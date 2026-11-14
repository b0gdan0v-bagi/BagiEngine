#include "FuzzProbeAccess.h"
#include "FuzzTargets.h"

#include <BECore/Reflection/XmlDeserializer.h>
#include <cmath>

namespace BECore::Fuzz {

    namespace {

        /// How many attributes and elements of a document the walk reads (every one is read with all 13 overloads of two kinds)
        constexpr size_t kWalkEntryBudget = 24;

        /// How deep the walk goes into nested elements
        constexpr size_t kWalkDepthLimit = 12;

        /// How many elements of one array the walk enters
        constexpr size_t kWalkArrayElementLimit = 3;

        /// The value a read target holds before the read; a read that fails must leave it as it is
        template <typename T>
        T Sentinel() {
            if constexpr (std::is_same_v<T, bool>) {
                return true;
            } else if constexpr (std::is_arithmetic_v<T>) {
                return static_cast<T>(42);
            } else {
                return T{};
            }
        }

        bool IsAsciiSpace(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        }

        eastl::string_view Trim(eastl::string_view text) {
            while (!text.empty() && IsAsciiSpace(text.front())) {
                text.remove_prefix(1);
            }
            while (!text.empty() && IsAsciiSpace(text.back())) {
                text.remove_suffix(1);
            }
            return text;
        }

        bool IsDigit(char c) {
            return c >= '0' && c <= '9';
        }

        /// What the strict reader has to say about a text, worked out here from the grammar and not from the reader's code:
        /// true (a value of T, and @p expected holds it), false (not a value of T) or nothing (a float that has the shape of a number: the reader decides)
        template <typename T>
        Optional<bool> StrictVerdict(eastl::string_view text, T& expected) {
            text = Trim(text);
            if constexpr (std::is_same_v<T, bool>) {
                if (text == "true" || text == "1") {
                    expected = true;
                    return true;
                }
                if (text == "false" || text == "0") {
                    expected = false;
                    return true;
                }
                return false;
            } else if constexpr (std::is_integral_v<T>) {
                size_t i = 0;
                bool negative = false;
                if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
                    negative = text[i] == '-';
                    ++i;
                }
                if (i == text.size()) {
                    return false;
                }
                uint64_t magnitude = 0;
                bool overflow = false;
                for (; i < text.size(); ++i) {
                    if (!IsDigit(text[i])) {
                        return false;
                    }
                    const auto digit = static_cast<uint64_t>(text[i] - '0');
                    if (magnitude > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
                        overflow = true;
                    } else {
                        magnitude = magnitude * 10 + digit;
                    }
                }
                if (overflow) {
                    return false;
                }
                if constexpr (std::is_unsigned_v<T>) {
                    if (negative || magnitude > std::numeric_limits<T>::max()) {
                        return false;
                    }
                    expected = static_cast<T>(magnitude);
                } else {
                    const auto largest = static_cast<uint64_t>(std::numeric_limits<T>::max());
                    if (!negative) {
                        if (magnitude > largest) {
                            return false;
                        }
                        expected = static_cast<T>(magnitude);
                    } else {
                        if (magnitude > largest + 1) {
                            return false;
                        }
                        expected = magnitude == largest + 1 ? std::numeric_limits<T>::min() : static_cast<T>(-static_cast<int64_t>(magnitude));
                    }
                }
                return true;
            } else {
                bool hasDigit = false;
                for (const char c : text) {
                    if (IsDigit(c)) {
                        hasDigit = true;
                    } else if (c != '+' && c != '-' && c != '.' && c != 'e' && c != 'E') {
                        return false;
                    }
                }
                if (!hasDigit) {
                    return false;
                }
                return std::nullopt;
            }
        }

        /// Reads one name with one overload and checks the answer against the text it has to read
        template <typename T>
        void CheckRead(XmlDeserializer& reader, eastl::string_view name, bool asAttribute, eastl::string_view text, bool strict) {
            const T sentinel = Sentinel<T>();
            T value = sentinel;
            const size_t errorsBefore = reader.GetErrors().size();
            const bool ok = asAttribute ? reader.ReadAttribute(name, value) : reader.Read(name, value);
            const size_t errorsAfter = reader.GetErrors().size();

            if constexpr (std::is_arithmetic_v<T>) {
                if (strict) {
                    T expected{};
                    const Optional<bool> verdict = StrictVerdict(text, expected);
                    if (verdict.has_value()) {
                        FUZZ_CHECK(ok == *verdict);
                        if (ok) {
                            FUZZ_CHECK(value == expected);
                        }
                    }
                    if (ok) {
                        FUZZ_CHECK(errorsAfter == errorsBefore);
                        if constexpr (std::is_floating_point_v<T>) {
                            FUZZ_CHECK(std::isfinite(value));
                        }
                    } else {
                        // A refused value is one error under the name, and the target keeps what it had
                        FUZZ_CHECK(errorsAfter == errorsBefore + 1);
                        FUZZ_CHECK(std::memcmp(&value, &sentinel, sizeof(T)) == 0);
                    }
                    return;
                }
                // The lenient reader finds the name and takes whatever number the text makes
                FUZZ_CHECK(ok);
                FUZZ_CHECK(errorsAfter == errorsBefore);
            } else {
                FUZZ_CHECK(ok);
                FUZZ_CHECK(errorsAfter == errorsBefore);
                if constexpr (std::is_same_v<T, PoolString>) {
                    FUZZ_CHECK(value.ToStringView() == text);
                } else {
                    FUZZ_CHECK(eastl::string_view(value.data(), value.size()) == text);
                }
            }
        }

        void CheckAllReads(XmlDeserializer& reader, eastl::string_view name, bool asAttribute, eastl::string_view text, bool strict) {
            CheckRead<bool>(reader, name, asAttribute, text, strict);
            CheckRead<int8_t>(reader, name, asAttribute, text, strict);
            CheckRead<uint8_t>(reader, name, asAttribute, text, strict);
            CheckRead<int16_t>(reader, name, asAttribute, text, strict);
            CheckRead<uint16_t>(reader, name, asAttribute, text, strict);
            CheckRead<int32_t>(reader, name, asAttribute, text, strict);
            CheckRead<uint32_t>(reader, name, asAttribute, text, strict);
            CheckRead<int64_t>(reader, name, asAttribute, text, strict);
            CheckRead<uint64_t>(reader, name, asAttribute, text, strict);
            CheckRead<float>(reader, name, asAttribute, text, strict);
            CheckRead<double>(reader, name, asAttribute, text, strict);
            CheckRead<eastl::string>(reader, name, asAttribute, text, strict);
            CheckRead<PoolString>(reader, name, asAttribute, text, strict);
        }

        eastl::string_view NameOf(const char* name) {
            return eastl::string_view(name);
        }

        /// Reads a loaded document by following a second parse of the same text: it needs no type, it asks the reader for what the document holds
        class DocumentWalker {
        public:
            DocumentWalker(XmlDeserializer& reader, bool strict) : _reader(reader), _strict(strict) {}

            void WalkScope(pugi::xml_node scope, size_t depth) {
                if (depth > kWalkDepthLimit) {
                    return;
                }

                for (const pugi::xml_attribute attribute : scope.attributes()) {
                    // A name resolves to the first attribute of that name
                    if (_budget == 0 || scope.attribute(attribute.name()) != attribute) {
                        continue;
                    }
                    --_budget;
                    CheckAllReads(_reader, NameOf(attribute.name()), true, NameOf(attribute.as_string()), _strict);
                    EndEntry();
                }

                // A name that is not in the document is not found, whatever it is read as
                FUZZ_CHECK(!_reader.BeginObject("fuzz-no-such-name"));
                size_t none = 0;
                FUZZ_CHECK(!_reader.BeginArray("fuzz-no-such-name", "item", none));

                for (const pugi::xml_node child : scope.children()) {
                    if (child.type() != pugi::node_element || _budget == 0 || scope.child(child.name()) != child) {
                        continue;
                    }
                    --_budget;
                    const eastl::string_view name = NameOf(child.name());
                    CheckAllReads(_reader, name, false, NameOf(child.text().as_string()), _strict);
                    EndEntry();

                    FUZZ_CHECK(_reader.BeginObject(name));
                    WalkScope(child, depth + 1);
                    _reader.EndObject();

                    WalkArray(child, name, depth);
                }
            }

        private:
            void WalkArray(pugi::xml_node arrayNode, eastl::string_view name, size_t depth) {
                size_t expectedCount = 0;
                for (const pugi::xml_node element : arrayNode.children()) {
                    (void)element;
                    ++expectedCount;
                }
                size_t count = 0;
                FUZZ_CHECK(_reader.BeginArray(name, "item", count));
                FUZZ_CHECK(count == expectedCount);

                size_t entered = 0;
                for (const pugi::xml_node element : arrayNode.children()) {
                    if (entered == kWalkArrayElementLimit) {
                        break;
                    }
                    FUZZ_CHECK(_reader.BeginArrayElement());
                    WalkScope(element, depth + 1);
                    _reader.EndArrayElement();
                    ++entered;
                }
                if (entered == count) {
                    FUZZ_CHECK(!_reader.BeginArrayElement());
                }
                _reader.EndArray();
            }

            /// The errors a probe reported are part of what was seen; dropping them keeps the list short
            void EndEntry() {
                _reader.ClearErrors();
            }

            XmlDeserializer& _reader;
            bool _strict;
            size_t _budget = kWalkEntryBudget;
        };

        /// After a refused load nothing is readable: the names of the earlier document are gone
        void ExpectNothingReadable(XmlDeserializer& reader, pugi::xml_node earlierRoot) {
            int32_t number = 0;
            size_t count = 0;
            FUZZ_CHECK(!reader.Read("fuzz-no-such-name", number));
            FUZZ_CHECK(!reader.BeginObject("fuzz-no-such-name"));
            FUZZ_CHECK(!reader.BeginArray("fuzz-no-such-name", "item", count));
            size_t checked = 0;
            for (const pugi::xml_node child : earlierRoot.children()) {
                if (child.type() == pugi::node_element && checked++ < kWalkArrayElementLimit) {
                    FUZZ_CHECK(!reader.BeginObject(NameOf(child.name())));
                }
            }
        }

        /// How many bytes the text sits behind the start of its buffer in the unaligned copy
        constexpr size_t kShift = 1;

        eastl::vector<uint8_t> ShiftedCopy(const uint8_t* data, size_t size) {
            eastl::vector<uint8_t> buffer(kShift + size);
            if (size > 0) {
                std::memcpy(buffer.data() + kShift, data, size);
            }
            return buffer;
        }

        uint64_t ReadProbeDigest(XmlDeserializer& reader) {
            ProbeState state;
            state.Deserialize(reader);
            reader.ClearErrors();
            return DigestProbe(state);
        }

        void CheckDocument(const uint8_t* data, size_t size, bool strict) {
            const eastl::string_view text(reinterpret_cast<const char*>(data), size);

            // The same text parsed by pugixml alone: the walk follows it, and a document it refuses is refused by the reader too
            pugi::xml_document reference;
            const bool parses = static_cast<bool>(reference.load_buffer(data, size, pugi::parse_default, pugi::encoding_utf8));

            XmlDeserializer reader;
            reader.SetStrictValues(strict);
            FUZZ_CHECK(reader.LoadFromString(text) == parses);
            if (!parses) {
                FUZZ_CHECK(reader.HasErrors());
                return;
            }
            FUZZ_CHECK(!reader.HasErrors());

            DocumentWalker walker(reader, strict);
            walker.WalkScope(reference.document_element(), 0);
            reader.ClearErrors();

            // A reflected type read from whatever the document holds: the same document gives the same state from a fresh reader and from this one again
            const uint64_t first = ReadProbeDigest(reader);
            XmlDeserializer fresh;
            fresh.SetStrictValues(strict);
            FUZZ_CHECK(fresh.LoadFromString(text));
            FUZZ_CHECK(ReadProbeDigest(fresh) == first);
            FUZZ_CHECK(ReadProbeDigest(reader) == first);

            // The same text in the middle of a buffer, where nothing is aligned: it loads and reads the same
            const eastl::vector<uint8_t> shifted = ShiftedCopy(data, size);
            XmlDeserializer unaligned;
            unaligned.SetStrictValues(strict);
            FUZZ_CHECK(unaligned.LoadFromString(eastl::string_view(reinterpret_cast<const char*>(shifted.data()) + kShift, size)));
            FUZZ_CHECK(ReadProbeDigest(unaligned) == first);

            // Loading something that does not parse on top of a good document drops it
            FUZZ_CHECK(!reader.LoadFromString("<"));
            ExpectNothingReadable(reader, reference.document_element());
            FUZZ_CHECK(reader.HasErrors());
        }

    }  // namespace

    void FuzzXmlDeserializer(const uint8_t* data, size_t size) {
        CheckDocument(data, size, false);
        CheckDocument(data, size, true);
    }

}  // namespace BECore::Fuzz
