#include "FuzzTargets.h"

#include <BECore/Reflection/BinaryDeserializer.h>

namespace BECore::Fuzz {

    namespace {

        /// How many entries of a document the walk reads (every one is read with all 26 overloads, so the cost of one input is bounded)
        constexpr size_t kWalkEntryBudget = 48;

        /// How many elements of one array the walk enters
        constexpr size_t kWalkArrayElementLimit = 4;

        constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ull;
        constexpr uint64_t kFnvPrime = 0x100000001B3ull;

        /// What a walk saw, as one number: two walks of the same buffer must agree on it
        class Digest {
        public:
            void AddBytes(const void* data, size_t size) {
                const auto* bytes = static_cast<const uint8_t*>(data);
                for (size_t i = 0; i < size; ++i) {
                    _value = (_value ^ bytes[i]) * kFnvPrime;
                }
            }

            template <typename T>
            void Add(const T& value) {
                AddBytes(&value, sizeof(T));
            }

            void AddText(eastl::string_view text) {
                Add(text.size());
                AddBytes(text.data(), text.size());
            }

            uint64_t Value() const {
                return _value;
            }

        private:
            uint64_t _value = kFnvOffset;
        };

        template <typename T>
        constexpr bool IsTextType = std::is_same_v<T, eastl::string> || std::is_same_v<T, PoolString>;

        template <typename T>
        constexpr uint8_t ExpectedTag() {
            if constexpr (IsTextType<T>) {
                return BinaryFormat::Tag::String;
            } else {
                return BinaryFormat::TagFor<T>();
            }
        }

        eastl::string_view TextOf(const eastl::string& value) {
            return eastl::string_view(value.data(), value.size());
        }

        eastl::string_view TextOf(const PoolString& value) {
            return value.ToStringView();
        }

        /// Whether what the reader returned is what the bytes of the entry say
        template <typename T>
        bool SameAsEntry(const T& value, const BinaryFormat::EntryView& entry, const uint8_t* data) {
            const uint8_t* payload = data + entry.payloadBegin;
            if constexpr (std::is_same_v<T, bool>) {
                return value == (*payload != 0);
            } else if constexpr (IsTextType<T>) {
                const size_t length = BinaryFormat::Load<uint32_t>(payload);
                return TextOf(value) == eastl::string_view(reinterpret_cast<const char*>(payload + sizeof(uint32_t)), length);
            } else {
                const T raw = BinaryFormat::Load<T>(payload);
                return std::memcmp(&raw, &value, sizeof(T)) == 0;
            }
        }

        template <typename T>
        void AddValue(Digest& digest, const T& value) {
            if constexpr (IsTextType<T>) {
                digest.AddText(TextOf(value));
            } else {
                digest.Add(value);
            }
        }

        /// The last entry of each kind that carries a name in one scope: the one a read of that name finds
        struct LastEntries {
            ptrdiff_t scalar = -1;
            ptrdiff_t object = -1;
            ptrdiff_t array = -1;
        };

        /// Reads a loaded document by following the bytes: it needs no type, it asks the reader for what the bytes hold
        class DocumentWalker {
        public:
            DocumentWalker(IDeserializer& reader, const uint8_t* data) : _reader(reader), _data(data) {}

            void WalkScope(size_t begin, size_t end, size_t depth) {
                FUZZ_CHECK(depth <= BinaryFormat::MaxDepth);

                eastl::vector<BinaryFormat::EntryView> entries;
                eastl::hash_map<eastl::string_view, LastEntries> last;
                for (size_t position = begin; position < end;) {
                    BinaryFormat::EntryView entry;
                    FUZZ_CHECK(BinaryFormat::ParseEntry(_data, position, end, entry));
                    LastEntries& slot = last[entry.name];
                    const auto index = static_cast<ptrdiff_t>(entries.size());
                    if (BinaryFormat::IsScalarTag(entry.tag)) {
                        slot.scalar = index;
                    } else if (BinaryFormat::IsObjectTag(entry.tag)) {
                        slot.object = index;
                    } else {
                        slot.array = index;
                    }
                    entries.push_back(entry);
                    position = entry.end;
                }

                for (size_t i = 0; i < entries.size() && _budget > 0; ++i) {
                    --_budget;
                    const BinaryFormat::EntryView& entry = entries[i];
                    const LastEntries& slot = last[entry.name];
                    const auto index = static_cast<ptrdiff_t>(i);

                    // A name resolves to its last entry of the kind, so only that entry is read
                    if (slot.scalar == index) {
                        ProbeScalar(entry);
                    }
                    if (slot.object == index) {
                        WalkObject(entry, depth);
                    } else if (slot.object < 0) {
                        FUZZ_CHECK(!_reader.BeginObject(entry.name));
                    }
                    if (slot.array == index) {
                        WalkArray(entry, depth);
                    } else if (slot.array < 0) {
                        size_t count = 0;
                        FUZZ_CHECK(!_reader.BeginArray(entry.name, "item", count));
                    }
                    EndEntry();
                }
            }

            Digest& GetDigest() {
                return _digest;
            }

        private:
            template <typename T>
            void ProbeType(const BinaryFormat::EntryView& entry) {
                const bool expected = entry.tag == ExpectedTag<T>();
                for (const bool asAttribute : {false, true}) {
                    T value{};
                    const bool ok = asAttribute ? _reader.ReadAttribute(entry.name, value) : _reader.Read(entry.name, value);
                    FUZZ_CHECK(ok == expected);
                    _digest.Add(ok);
                    if (ok) {
                        FUZZ_CHECK(SameAsEntry(value, entry, _data));
                        AddValue(_digest, value);
                    }
                }
            }

            /// Every Read / ReadAttribute overload for the name of a scalar entry: only the one of its own type may succeed
            void ProbeScalar(const BinaryFormat::EntryView& entry) {
                ProbeType<bool>(entry);
                ProbeType<int8_t>(entry);
                ProbeType<uint8_t>(entry);
                ProbeType<int16_t>(entry);
                ProbeType<uint16_t>(entry);
                ProbeType<int32_t>(entry);
                ProbeType<uint32_t>(entry);
                ProbeType<int64_t>(entry);
                ProbeType<uint64_t>(entry);
                ProbeType<float>(entry);
                ProbeType<double>(entry);
                ProbeType<eastl::string>(entry);
                ProbeType<PoolString>(entry);
            }

            void WalkObject(const BinaryFormat::EntryView& entry, size_t depth) {
                FUZZ_CHECK(_reader.BeginObject(entry.name));
                _digest.AddText(entry.name);
                WalkScope(entry.payloadBegin + sizeof(uint32_t), entry.end, depth + 1);
                _reader.EndObject();
            }

            void WalkArray(const BinaryFormat::EntryView& entry, size_t depth) {
                const size_t storedCount = BinaryFormat::Load<uint32_t>(_data + entry.payloadBegin + sizeof(uint32_t));
                size_t count = 0;
                FUZZ_CHECK(_reader.BeginArray(entry.name, "item", count));
                FUZZ_CHECK(count == storedCount);
                _digest.AddText(entry.name);
                _digest.Add(count);

                size_t position = entry.payloadBegin + 2 * sizeof(uint32_t);
                const size_t walked = std::min(count, kWalkArrayElementLimit);
                for (size_t i = 0; i < walked; ++i) {
                    const size_t elementSize = BinaryFormat::Load<uint32_t>(_data + position);
                    position += sizeof(uint32_t);
                    FUZZ_CHECK(_reader.BeginArrayElement());
                    WalkScope(position, position + elementSize, depth + 1);
                    _reader.EndArrayElement();
                    position += elementSize;
                }
                if (walked == count) {
                    FUZZ_CHECK(!_reader.BeginArrayElement());
                }
                _reader.EndArray();
            }

            /// The errors a probe reported are part of what was seen; dropping them keeps the list (and the pool of strings) short
            void EndEntry() {
                _digest.Add(_reader.GetErrors().size());
                _reader.ClearErrors();
            }

            IDeserializer& _reader;
            const uint8_t* _data;
            Digest _digest;
            size_t _budget = kWalkEntryBudget;
        };

        uint64_t WalkDocument(BinaryDeserializer& reader, const uint8_t* data, size_t size) {
            DocumentWalker walker(reader, data);
            walker.WalkScope(BinaryFormat::HeaderSize, size, 0);
            return walker.GetDigest().Value();
        }

        /// A reader that refused its input exposes nothing: every read fails and no scope opens
        void ExpectNothingReadable(BinaryDeserializer& reader) {
            int32_t number = 0;
            eastl::string text;
            size_t count = 0;
            FUZZ_CHECK(!reader.Read("a", number));
            FUZZ_CHECK(!reader.ReadAttribute("a", text));
            FUZZ_CHECK(!reader.BeginObject("a"));
            FUZZ_CHECK(!reader.BeginArray("a", "b", count));
            FUZZ_CHECK(!reader.BeginArrayElement());
        }

        void CheckBuffer(const uint8_t* data, size_t size) {
            BinaryDeserializer reader;
            if (!reader.LoadFromBuffer(data, size)) {
                FUZZ_CHECK(reader.HasErrors());
                ExpectNothingReadable(reader);
                // A refusal is not a state: the same bytes are refused again by a fresh reader
                BinaryDeserializer again;
                FUZZ_CHECK(!again.LoadFromBuffer(data, size));
                return;
            }

            reader.ClearErrors();
            const uint64_t first = WalkDocument(reader, data, size);

            // The same buffer through a fresh reader and through the reader that has just been walked: no hidden state
            BinaryDeserializer fresh;
            FUZZ_CHECK(fresh.LoadFromBuffer(data, size));
            FUZZ_CHECK(WalkDocument(fresh, data, size) == first);

            reader.ClearErrors();
            FUZZ_CHECK(reader.LoadFromBuffer(data, size));
            FUZZ_CHECK(WalkDocument(reader, data, size) == first);

            // Loading something else on top of a good document drops it
            FUZZ_CHECK(!reader.LoadFromBuffer(nullptr, 0));
            ExpectNothingReadable(reader);
        }

    }  // namespace

    void FuzzBinaryDeserializer(const uint8_t* data, size_t size) {
        CheckBuffer(data, size);

        // Any change of the length breaks the payload size of the header, and the load stops there, before the checks that matter.
        // The same bytes under a header that is true reach the validation of the entries, which is what is being fuzzed
        if (size >= BinaryFormat::HeaderSize) {
            eastl::vector<uint8_t> repaired(data, data + size);
            std::memcpy(repaired.data(), BinaryFormat::Magic, sizeof(BinaryFormat::Magic));
            BinaryFormat::Store<uint16_t>(repaired.data() + sizeof(BinaryFormat::Magic), BinaryFormat::CurrentVersion);
            BinaryFormat::Store<uint32_t>(repaired.data() + BinaryFormat::PayloadSizeOffset, static_cast<uint32_t>(size - BinaryFormat::HeaderSize));
            CheckBuffer(repaired.data(), repaired.size());
        }
    }

}  // namespace BECore::Fuzz
