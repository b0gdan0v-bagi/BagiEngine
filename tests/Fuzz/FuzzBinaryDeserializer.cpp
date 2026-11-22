#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/SchemaHash.h>
#include <Fuzz/FuzzChecks.h>
#include <Fuzz/FuzzTargets.h>

namespace BECore::Fuzz {

    namespace {

        /// How many entries of a document the walk reads (every one is read with all 26 overloads, so the cost of one input is bounded)
        constexpr size_t kWalkEntryBudget = 48;

        /// How many elements of one array the walk enters
        constexpr size_t kWalkArrayElementLimit = 4;

        template <typename T>
        constexpr bool IsTextType = std::is_same_v<T, String> || std::is_same_v<T, PoolString>;

        template <typename T>
        constexpr uint8_t ExpectedTag() {
            static_assert(!IsTextType<T>);
            return BinaryFormat::TagFor<T>();
        }

        /// A String and a PoolString read either text tag
        template <typename T>
        bool IsTagOf(uint8_t tag) {
            if constexpr (IsTextType<T>) {
                return BinaryFormat::IsTextTag(tag);
            } else {
                return tag == ExpectedTag<T>();
            }
        }

        StringView TextOf(const String& value) {
            return StringView(value);
        }

        StringView TextOf(const PoolString& value) {
            return value.ToStringView();
        }

        /// Whether what the reader returned is what the bytes of the entry say
        template <typename T>
        bool SameAsEntry(const T& value, const BinaryFormat::EntryView& entry, const uint8_t* data, const BinaryFormat::StringTable& table) {
            const uint8_t* payload = data + entry.payloadBegin;
            if constexpr (std::is_same_v<T, bool>) {
                return value == (*payload != 0);
            } else if constexpr (IsTextType<T>) {
                if (entry.tag == BinaryFormat::Tag::PoolString) {
                    return TextOf(value) == table.Get(BinaryFormat::Load<uint32_t>(payload));
                }
                const size_t length = BinaryFormat::Load<uint32_t>(payload);
                return TextOf(value) == StringView(reinterpret_cast<const char*>(payload + sizeof(uint32_t)), length);
            } else {
                const T raw = BinaryFormat::Load<T>(payload);
                return std::memcmp(&raw, &value, sizeof(T)) == 0;
            }
        }

        template <typename T>
        void AddValue(FuzzDigest& digest, const T& value) {
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
            /// @p table is the string table of a version 2 document, null for version 1
            DocumentWalker(IDeserializer& reader, const uint8_t* data, const BinaryFormat::StringTable* table) : _reader(reader), _data(data), _table(table) {}

            void WalkScope(size_t begin, size_t end, size_t depth) {
                FUZZ_CHECK(depth <= BinaryFormat::MaxDepth);

                Vector<BinaryFormat::EntryView> entries;
                HashMap<StringView, LastEntries> last;
                for (size_t position = begin; position < end;) {
                    BinaryFormat::EntryView entry;
                    FUZZ_CHECK(_table != nullptr ? BinaryFormat::ParseEntryV2(_data, position, end, *_table, entry) : BinaryFormat::ParseEntry(_data, position, end, entry));
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

            FuzzDigest& GetDigest() {
                return _digest;
            }

        private:
            template <typename T>
            void ProbeType(const BinaryFormat::EntryView& entry) {
                const bool expected = IsTagOf<T>(entry.tag);
                for (const bool asAttribute : {false, true}) {
                    T value{};
                    const bool ok = asAttribute ? _reader.ReadAttribute(entry.name, value) : _reader.Read(entry.name, value);
                    FUZZ_CHECK(ok == expected);
                    _digest.Add(ok);
                    if (ok) {
                        FUZZ_CHECK(SameAsEntry(value, entry, _data, _table != nullptr ? *_table : BinaryFormat::StringTable{}));
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
                ProbeType<String>(entry);
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
            const BinaryFormat::StringTable* _table;
            FuzzDigest _digest;
            size_t _budget = kWalkEntryBudget;
        };

        /// Walks the entries of a document the reader accepted, by the layout of its version: after the header (version 1) or after the table (version 2)
        uint64_t WalkDocument(BinaryDeserializer& reader, const uint8_t* data, size_t size) {
            if (BinaryFormat::Load<uint16_t>(data + sizeof(BinaryFormat::Magic)) == BinaryFormat::TabledVersion) {
                BinaryFormat::StringTable table;
                size_t tableEnd = 0;
                // The reader validated the table, so the helper that the reader is built on parses it too
                FUZZ_CHECK(BinaryFormat::ParseStringTable(data, BinaryFormat::HeaderSize, size, table, tableEnd));
                DocumentWalker walker(reader, data, &table);
                walker.WalkScope(tableEnd, size, 0);
                return walker.GetDigest().Value();
            }
            DocumentWalker walker(reader, data, nullptr);
            walker.WalkScope(BinaryFormat::HeaderSize, size, 0);
            return walker.GetDigest().Value();
        }

        /// A reader that refused its input exposes nothing: every read fails and no scope opens
        void ExpectNothingReadable(BinaryDeserializer& reader) {
            int32_t number = 0;
            String text;
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

                // The view reads the caller's memory and refuses the same bytes
                BinaryDeserializer view;
                FUZZ_CHECK(!view.LoadFromView(data, size));
                FUZZ_CHECK(view.HasErrors());
                ExpectNothingReadable(view);
                return;
            }

            reader.ClearErrors();
            // The stamp of a content file is a question about any accepted document: an answer either way, never an error on the reader
            const std::expected<ContentStamp, String> stamp = ReadContentStamp(reader);
            FUZZ_CHECK(!reader.HasErrors());
            FUZZ_CHECK(stamp.has_value() || !stamp.error().empty());
            const uint64_t first = WalkDocument(reader, data, size);

            // The same buffer through a fresh reader and through the reader that has just been walked: no hidden state
            BinaryDeserializer fresh;
            FUZZ_CHECK(fresh.LoadFromBuffer(data, size));
            FUZZ_CHECK(WalkDocument(fresh, data, size) == first);

            // The view is the input itself, not a copy: the same document and the same digest
            BinaryDeserializer view;
            FUZZ_CHECK(view.LoadFromView(data, size));
            FUZZ_CHECK(WalkDocument(view, data, size) == first);

            reader.ClearErrors();
            FUZZ_CHECK(reader.LoadFromBuffer(data, size));
            FUZZ_CHECK(WalkDocument(reader, data, size) == first);

            // Loading something else on top of a good document drops it
            FUZZ_CHECK(!reader.LoadFromBuffer(nullptr, 0));
            ExpectNothingReadable(reader);
            FUZZ_CHECK(!view.LoadFromView(nullptr, 0));
            ExpectNothingReadable(view);
        }

    }  // namespace

    void FuzzBinaryDeserializer(const uint8_t* data, size_t size) {
        CheckBuffer(data, size);

        const Vector<uint8_t> repaired = WithTrueBinaryHeader(data, size);
        if (!repaired.empty()) {
            CheckBuffer(repaired.data(), repaired.size());
        }
    }

}  // namespace BECore::Fuzz
