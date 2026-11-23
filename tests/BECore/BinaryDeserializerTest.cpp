#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Common/AllocCheck.h>
#include <Common/BinaryStreams.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <random>

namespace BECore::Tests {

    struct BinaryDeserializerAccess {
        static bool HasDuplicateNames(const BinaryDeserializer& deserializer) {
            return deserializer._hasDuplicateNames;
        }
    };

    namespace {

        constexpr size_t kSpriteSizedElements = 2000;
        constexpr uint64_t kBoundedLoadBlocks = 8;
        /// Mirrors the validation's inline key count and entry cap (BinaryDeserializer.cpp): a test of the edges of those
        constexpr size_t kInlineKeyLimit = 16;
        constexpr size_t kMaxTrackedEntriesForTest = 65536;
        constexpr int32_t kNumber = 42;
        constexpr StringView kText = "AAAA";

        /// A document `doc { number, text, items[count] { id, label } }`, the shape of a library file
        void BuildDocument(BinarySerializer& serializer, size_t elements, StringView text = kText) {
            serializer.BeginObject("doc");
            serializer.Write("number", kNumber);
            serializer.Write("text", text);
            size_t count = elements;
            serializer.BeginArray("items", "item", count);
            for (size_t i = 0; i < elements; ++i) {
                serializer.BeginArrayElement();
                serializer.Write("id", static_cast<int32_t>(i));
                serializer.Write("label", Format("label_{}", i));
                serializer.EndArrayElement();
            }
            serializer.EndArray();
            serializer.EndObject();
        }

        Vector<uint8_t> MakeStream(size_t elements, StringView text = kText) {
            BinarySerializer serializer;
            BuildDocument(serializer, elements, text);
            return serializer.GetData();
        }

        /// Reads `doc` the way MakeStream wrote it and checks every value; the first failure fails the case
        void ExpectDocument(BinaryDeserializer& deserializer, size_t elements, StringView text = kText) {
            ASSERT_TRUE(deserializer.BeginObject("doc"));
            int32_t number = 0;
            ASSERT_TRUE(deserializer.Read("number", number));
            EXPECT_EQ(number, kNumber);
            String readText;
            ASSERT_TRUE(deserializer.Read("text", readText));
            EXPECT_EQ(readText, String(text));

            size_t count = 0;
            ASSERT_TRUE(deserializer.BeginArray("items", "item", count));
            ASSERT_EQ(count, elements);
            for (size_t i = 0; i < elements; ++i) {
                ASSERT_TRUE(deserializer.BeginArrayElement());
                int32_t id = -1;
                EXPECT_TRUE(deserializer.Read("id", id));
                EXPECT_EQ(id, static_cast<int32_t>(i));
                deserializer.EndArrayElement();
            }
            deserializer.EndArray();
            deserializer.EndObject();
        }

        bool NothingReadable(BinaryDeserializer& deserializer) {
            int32_t value = 0;
            return !deserializer.HasKey("doc") && !deserializer.BeginObject("doc") && !deserializer.Read("doc", value);
        }

        /// The index of the first byte of @p needle in @p bytes, or the size when it is not there
        size_t IndexOf(const Vector<uint8_t>& bytes, StringView needle) {
            for (size_t i = 0; i + needle.size() <= bytes.size(); ++i) {
                if (std::memcmp(bytes.data() + i, needle.data(), needle.size()) == 0) {
                    return i;
                }
            }
            return bytes.size();
        }

    }  // namespace

    TEST(BinaryDeserializerTest, AViewReadsTheMemoryOfTheCaller) {
        // This proves only that nothing is copied: a caller must not rely on changing bytes behind a loaded reader
        Vector<uint8_t> bytes = MakeStream(3);
        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromView(bytes.data(), bytes.size()));

        const size_t at = IndexOf(bytes, kText);
        ASSERT_LT(at, bytes.size());
        bytes[at] = 'B';

        ASSERT_TRUE(deserializer.BeginObject("doc"));
        String text;
        ASSERT_TRUE(deserializer.Read("text", text));
        EXPECT_EQ(text, "BAAA");
    }

    TEST(BinaryDeserializerTest, ABufferIsCopiedSoTheSourceMayChange) {
        Vector<uint8_t> bytes = MakeStream(3);
        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(bytes));
        for (uint8_t& byte : bytes) {
            byte = 0;
        }
        ExpectDocument(deserializer, 3);
    }

    TEST(BinaryDeserializerTest, AFileReadsTheSameAsTheBufferOfTheSameBytes) {
        const std::filesystem::path path = TestTempPath("binary_deserializer_file");
        BinarySerializer document;
        BuildDocument(document, 50);
        const bool saved = document.SaveToFile(path);

        BinaryDeserializer fromFile;
        const bool fileOpened = saved && fromFile.LoadFromFile(path);
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        ASSERT_TRUE(fileOpened);
        ExpectDocument(fromFile, 50);

        BinaryDeserializer fromBuffer;
        ASSERT_TRUE(fromBuffer.LoadFromBuffer(MakeStream(50)));
        ExpectDocument(fromBuffer, 50);
    }

    TEST(BinaryDeserializerTest, ARefusedLoadLeavesNothingReadable) {
        const Vector<uint8_t> good = MakeStream(3);
        Vector<uint8_t> badMagic = good;
        badMagic[0] = 'X';
        const Vector<uint8_t> truncated(good.begin(), good.end() - 1);

        struct Case {
            const char* name;
            const uint8_t* data;
            size_t size;
        };
        const Case cases[] = {{"bad magic", badMagic.data(), badMagic.size()}, {"truncated", truncated.data(), truncated.size()}, {"nullptr", nullptr, good.size()}, {"empty", good.data(), 0}};
        for (const Case& refused : cases) {
            BinaryDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromView(good.data(), good.size())) << refused.name;
            EXPECT_FALSE(deserializer.LoadFromView(refused.data, refused.size)) << refused.name;
            EXPECT_TRUE(deserializer.HasErrors()) << refused.name;
            EXPECT_TRUE(NothingReadable(deserializer)) << refused.name;

            ASSERT_TRUE(deserializer.LoadFromBuffer(good));
            EXPECT_FALSE(deserializer.LoadFromBuffer(refused.data, refused.size)) << refused.name;
            EXPECT_TRUE(NothingReadable(deserializer)) << refused.name;
        }

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(good));
        EXPECT_FALSE(deserializer.LoadFromFile(TestTempPath("binary_deserializer_missing")));
        EXPECT_TRUE(NothingReadable(deserializer));
    }

    TEST(BinaryDeserializerTest, EachLoadReplacesTheOneBefore) {
        const Vector<uint8_t> first = MakeStream(2, "AAAA");
        const Vector<uint8_t> second = MakeStream(5, "BBBB");
        const Vector<uint8_t> third = MakeStream(1, "CCCC");
        const std::filesystem::path path = TestTempPath("binary_deserializer_replace");
        BinarySerializer document;
        BuildDocument(document, 0, "DDDD");
        const bool saved = document.SaveToFile(path);

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromView(first.data(), first.size()));
        ExpectDocument(deserializer, 2, "AAAA");
        ASSERT_TRUE(deserializer.LoadFromBuffer(second));
        ExpectDocument(deserializer, 5, "BBBB");
        ASSERT_TRUE(deserializer.LoadFromView(third.data(), third.size()));
        ExpectDocument(deserializer, 1, "CCCC");
        const bool fileOpened = saved && deserializer.LoadFromFile(path);
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        ASSERT_TRUE(fileOpened);
        ExpectDocument(deserializer, 0, "DDDD");
        ASSERT_TRUE(deserializer.LoadFromView(first.data(), first.size()));
        ExpectDocument(deserializer, 2, "AAAA");
    }

    TEST(BinaryDeserializerTest, AMovedReaderStillReads) {
        const Vector<uint8_t> bytes = MakeStream(4);

        BinaryDeserializer owning;
        ASSERT_TRUE(owning.LoadFromBuffer(bytes));
        BinaryDeserializer movedOwning(eastl::move(owning));
        ExpectDocument(movedOwning, 4);

        BinaryDeserializer viewing;
        ASSERT_TRUE(viewing.LoadFromView(bytes.data(), bytes.size()));
        BinaryDeserializer movedViewing;
        movedViewing = eastl::move(viewing);
        ExpectDocument(movedViewing, 4);

        BinaryDeserializer assigned;
        ASSERT_TRUE(assigned.LoadFromBuffer(MakeStream(1)));
        ASSERT_TRUE(owning.LoadFromBuffer(bytes));
        assigned = eastl::move(owning);
        ExpectDocument(assigned, 4);
    }

    TEST(BinaryDeserializerTest, AViewOfASpriteSizedStreamAllocatesNotTheStream) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        } else {
            const Vector<uint8_t> bytes = MakeStream(kSpriteSizedElements);
            BinaryDeserializer deserializer;
            bool viewed = false;
            const AllocCount view = CountAllocations([&] { viewed = deserializer.LoadFromView(bytes.data(), bytes.size()); });
            ASSERT_TRUE(viewed);
            EXPECT_LE(view.count, kBoundedLoadBlocks);
            EXPECT_LT(view.bytes, bytes.size());

            BinaryDeserializer copying;
            bool copied = false;
            const AllocCount copy = CountAllocations([&] { copied = copying.LoadFromBuffer(bytes); });
            ASSERT_TRUE(copied);
            EXPECT_GE(copy.bytes, bytes.size());
        }
    }

    // =============================================================================
    // The lookup from the last match gives what a scan for the last entry of the name gives
    // =============================================================================

    namespace {

        CORE_ENUM(NameKind, uint8_t, Scalar, Object, Array)

        bool IsKind(NameKind kind, uint8_t tag) {
            switch (kind) {
                case NameKind::Scalar:
                    return BinaryFormat::IsScalarTag(tag);
                case NameKind::Object:
                    return BinaryFormat::IsObjectTag(tag);
                default:
                    return BinaryFormat::IsArrayTag(tag);
            }
        }

        struct Range {
            size_t begin = 0;
            size_t end = 0;
        };

        /// A stream the reference scan reads: its bytes, the string table of version 2 and where the document's entries start
        struct Doc {
            const Vector<uint8_t>& bytes;
            bool tabled = false;
            BinaryFormat::StringTable table;
            size_t rootBegin = BinaryFormat::HeaderSize;
            bool pooled = false;
            Vector<String> pool;  ///< The texts of the pool section of version 3
        };

        Doc DocOf(const Vector<uint8_t>& bytes) {
            Doc doc{bytes};
            const uint16_t version = BinaryFormat::Load<uint16_t>(bytes.data() + sizeof(BinaryFormat::Magic));
            doc.tabled = version != BinaryFormat::LegacyVersion;
            doc.pooled = version == BinaryFormat::PooledVersion;
            if (doc.tabled && !BinaryFormat::ParseStringTable(bytes.data(), BinaryFormat::HeaderSize, bytes.size(), doc.table, doc.rootBegin)) {
                ADD_FAILURE() << "the string table is not well formed";
            }
            if (doc.pooled) {
                BinaryFormat::PoolSection section;
                if (!BinaryFormat::ParsePoolSection(bytes.data(), doc.rootBegin, bytes.size(), section, doc.rootBegin)) {
                    ADD_FAILURE() << "the pool section is not well formed";
                    return doc;
                }
                size_t at = section.entriesOffset;
                for (uint32_t i = 0; i < section.count; ++i) {
                    const size_t size = BinaryFormat::Load<uint32_t>(bytes.data() + at + offsetof(Details::PoolStringEntry, size));
                    doc.pool.emplace_back(reinterpret_cast<const char*>(bytes.data() + at + offsetof(Details::PoolStringEntry, data)), size);
                    at += Details::PoolEntryBytes(size);
                }
            }
            return doc;
        }

        Vector<BinaryFormat::EntryView> EntriesOf(const Doc& doc, Range range) {
            Vector<BinaryFormat::EntryView> entries;
            size_t position = range.begin;
            while (position < range.end) {
                BinaryFormat::EntryView entry;
                bool parsed = false;
                if (doc.pooled) {
                    parsed = BinaryFormat::ParseEntryV3(doc.bytes.data(), position, range.end, doc.table, doc.pool.size(), entry);
                } else if (doc.tabled) {
                    parsed = BinaryFormat::ParseEntryV2(doc.bytes.data(), position, range.end, doc.table, entry);
                } else {
                    parsed = BinaryFormat::ParseEntry(doc.bytes.data(), position, range.end, entry);
                }
                if (!parsed) {
                    ADD_FAILURE() << "the document is not well formed";
                    break;
                }
                entries.push_back(entry);
                position = entry.end;
            }
            return entries;
        }

        /// The reference: the last entry of the kind with the name, found by scanning the whole scope
        const BinaryFormat::EntryView* LastOf(const Vector<BinaryFormat::EntryView>& entries, StringView name, NameKind kind) {
            const BinaryFormat::EntryView* last = nullptr;
            for (const BinaryFormat::EntryView& entry : entries) {
                if (IsKind(kind, entry.tag) && entry.name == name) {
                    last = &entry;
                }
            }
            return last;
        }

        CORE_ENUM(ReadOrder, uint8_t, Written, Reversed, Shuffled, EachTwice, EveryOther)

        struct Item {
            String name;
            NameKind kind = NameKind::Scalar;
        };

        constexpr uint32_t kShuffleSeed = 20240607;

        /// The distinct (name, kind) pairs of the scope in the order the writer wrote them first, reordered by @p order
        Vector<Item> PlanOf(const Vector<BinaryFormat::EntryView>& entries, ReadOrder order, uint32_t seed) {
            Vector<Item> items;
            for (const BinaryFormat::EntryView& entry : entries) {
                const NameKind kind = BinaryFormat::IsScalarTag(entry.tag) ? NameKind::Scalar : (BinaryFormat::IsObjectTag(entry.tag) ? NameKind::Object : NameKind::Array);
                bool known = false;
                for (const Item& item : items) {
                    known = known || (item.kind == kind && StringView(item.name) == entry.name);
                }
                if (!known) {
                    items.push_back(Item{String(entry.name), kind});
                }
            }
            Vector<Item> plan;
            switch (order) {
                case ReadOrder::Reversed:
                    for (size_t i = items.size(); i > 0; --i) {
                        plan.push_back(items[i - 1]);
                    }
                    break;
                case ReadOrder::Shuffled: {
                    std::mt19937 random(seed);
                    for (size_t i = items.size(); i > 1; --i) {
                        eastl::swap(items[i - 1], items[random() % i]);
                    }
                    plan = items;
                    break;
                }
                case ReadOrder::EachTwice:
                    for (const Item& item : items) {
                        plan.push_back(item);
                        plan.push_back(item);
                    }
                    break;
                case ReadOrder::EveryOther:
                    for (size_t i = 0; i < items.size(); i += 2) {
                        plan.push_back(items[i]);
                    }
                    break;
                default:
                    plan = items;
                    break;
            }
            return plan;
        }

        template <typename T>
        void ExpectScalarEqual(BinaryDeserializer& reader, const BinaryFormat::EntryView& entry, const uint8_t* data, const String& where) {
            T value{};
            ASSERT_TRUE(reader.ReadAttribute(entry.name, value)) << where.c_str();
            if constexpr (std::is_same_v<T, bool>) {
                EXPECT_EQ(value, data[entry.payloadBegin] != 0) << where.c_str();
            } else {
                const T expected = BinaryFormat::Load<T>(data + entry.payloadBegin);
                EXPECT_EQ(std::memcmp(&value, &expected, sizeof(T)), 0) << where.c_str();
            }
        }

        void ExpectScalarRead(BinaryDeserializer& reader, const BinaryFormat::EntryView& entry, const Doc& doc, const String& where) {
            const uint8_t* data = doc.bytes.data();
            switch (entry.tag) {
                case BinaryFormat::Tag::Bool:
                    return ExpectScalarEqual<bool>(reader, entry, data, where);
                case BinaryFormat::Tag::Int8:
                    return ExpectScalarEqual<int8_t>(reader, entry, data, where);
                case BinaryFormat::Tag::UInt8:
                    return ExpectScalarEqual<uint8_t>(reader, entry, data, where);
                case BinaryFormat::Tag::Int16:
                    return ExpectScalarEqual<int16_t>(reader, entry, data, where);
                case BinaryFormat::Tag::UInt16:
                    return ExpectScalarEqual<uint16_t>(reader, entry, data, where);
                case BinaryFormat::Tag::Int32:
                    return ExpectScalarEqual<int32_t>(reader, entry, data, where);
                case BinaryFormat::Tag::UInt32:
                    return ExpectScalarEqual<uint32_t>(reader, entry, data, where);
                case BinaryFormat::Tag::Int64:
                    return ExpectScalarEqual<int64_t>(reader, entry, data, where);
                case BinaryFormat::Tag::UInt64:
                    return ExpectScalarEqual<uint64_t>(reader, entry, data, where);
                case BinaryFormat::Tag::Float:
                    return ExpectScalarEqual<float>(reader, entry, data, where);
                case BinaryFormat::Tag::Double:
                    return ExpectScalarEqual<double>(reader, entry, data, where);
                default: {
                    String text;
                    ASSERT_TRUE(reader.ReadAttribute(entry.name, text)) << where.c_str();
                    const size_t length = BinaryFormat::Load<uint32_t>(data + entry.payloadBegin);
                    StringView expected = StringView(reinterpret_cast<const char*>(data + entry.payloadBegin + sizeof(uint32_t)), length);
                    if (entry.tag == BinaryFormat::Tag::PoolString) {
                        expected = doc.pooled ? StringView(doc.pool[length]) : doc.table.Get(length);
                    }
                    EXPECT_EQ(StringView(text), expected) << where.c_str();
                }
            }
        }

        void WalkScope(BinaryDeserializer& reader, const Doc& doc, Range range, ReadOrder order, const String& where);

        void ExpectNameAbsent(BinaryDeserializer& reader, StringView name, NameKind kind, const String& where) {
            int32_t number = 0;
            size_t count = 0;
            switch (kind) {
                case NameKind::Scalar:
                    EXPECT_FALSE(reader.ReadAttribute(name, number)) << where.c_str();
                    break;
                case NameKind::Object:
                    EXPECT_FALSE(reader.BeginObject(name)) << where.c_str();
                    break;
                default:
                    EXPECT_FALSE(reader.BeginArray(name, "item", count)) << where.c_str();
                    break;
            }
        }

        /// Reads every entry of the scope of @p range in the order of @p order and compares it with the reference scan of the bytes
        void WalkScope(BinaryDeserializer& reader, const Doc& doc, Range range, ReadOrder order, const String& where) {
            const Vector<BinaryFormat::EntryView> entries = EntriesOf(doc, range);
            const Vector<Item> plan = PlanOf(entries, order, kShuffleSeed);
            for (size_t step = 0; step < plan.size(); ++step) {
                const Item& item = plan[step];
                const String here = Format("{}/{} (order {})", where, item.name, static_cast<int>(order));
                const BinaryFormat::EntryView* expected = LastOf(entries, item.name, item.kind);
                ASSERT_NE(expected, nullptr);

                EXPECT_TRUE(reader.HasKey(item.name)) << here.c_str();
                // A name that is not in the scope, asked in the middle of the walk, must not send the next read astray
                ExpectNameAbsent(reader, Format("missing_{}", step), NameKind::Scalar, here);
                EXPECT_FALSE(reader.HasKey(Format("missing_{}", step))) << here.c_str();

                for (const NameKind kind : {NameKind::Scalar, NameKind::Object, NameKind::Array}) {
                    if (kind != item.kind) {
                        // The same name as another kind of entry: present in the reference or not, the lookup agrees
                        if (LastOf(entries, item.name, kind) == nullptr) {
                            ExpectNameAbsent(reader, item.name, kind, here);
                        }
                    }
                }

                if (item.kind == NameKind::Scalar) {
                    ExpectScalarRead(reader, *expected, doc, here);
                } else if (item.kind == NameKind::Object) {
                    ASSERT_TRUE(reader.BeginObject(item.name)) << here.c_str();
                    WalkScope(reader, doc, Range{expected->payloadBegin + sizeof(uint32_t), expected->end}, order, here);
                    reader.EndObject();
                } else {
                    size_t count = 0;
                    ASSERT_TRUE(reader.BeginArray(item.name, "item", count)) << here.c_str();
                    const size_t countOffset = expected->payloadBegin + sizeof(uint32_t);
                    ASSERT_EQ(count, BinaryFormat::Load<uint32_t>(doc.bytes.data() + countOffset)) << here.c_str();
                    size_t position = countOffset + sizeof(uint32_t);
                    for (size_t i = 0; i < count; ++i) {
                        const size_t size = BinaryFormat::Load<uint32_t>(doc.bytes.data() + position);
                        ASSERT_TRUE(reader.BeginArrayElement()) << here.c_str();
                        WalkScope(reader, doc, Range{position + sizeof(uint32_t), position + sizeof(uint32_t) + size}, order, Format("{}[{}]", here, i));
                        reader.EndArrayElement();
                        position += sizeof(uint32_t) + size;
                    }
                    EXPECT_FALSE(reader.BeginArrayElement()) << here.c_str();
                    reader.EndArray();
                }
            }
        }

        /// Loads @p bytes once per order and walks the whole document against the scan
        void ExpectLookupMatchesStream(const Vector<uint8_t>& bytes, bool expectDuplicateFlag, const char* what) {
            const Doc doc = DocOf(bytes);
            for (const ReadOrder order : {ReadOrder::Written, ReadOrder::Reversed, ReadOrder::Shuffled, ReadOrder::EachTwice, ReadOrder::EveryOther}) {
                BinaryDeserializer reader;
                ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size())) << what;
                EXPECT_EQ(BinaryDeserializerAccess::HasDuplicateNames(reader), expectDuplicateFlag) << what;
                WalkScope(reader, doc, Range{doc.rootBegin, bytes.size()}, order, what);
            }
        }

        /// The document as it is, and the same entries written again as versions 2 and 3: each must read as the scan says
        void ExpectLookupMatchesScan(const Vector<uint8_t>& bytes, bool expectDuplicateFlag, const char* what) {
            ExpectLookupMatchesStream(bytes, expectDuplicateFlag, what);
            const Vector<BinaryStreams::Entry> tree = BinaryStreams::ReadTree(bytes);
            for (const uint16_t version : {BinaryFormat::TabledVersion, BinaryFormat::PooledVersion}) {
                BinaryStreams::Options options;
                options.version = version;
                ExpectLookupMatchesStream(BinaryStreams::Build(tree, options), expectDuplicateFlag, Format("{} (version {})", what, version).c_str());
            }
        }

        void WriteElementFields(BinarySerializer& writer, int32_t id) {
            writer.Write("id", id);
            writer.Write("label", Format("label_{}", id));
            writer.BeginObject("pos");
            writer.Write("x", static_cast<float>(id) * 0.5f);
            writer.Write("y", static_cast<float>(id) * 1.5f);
            writer.EndObject();
            size_t tagCount = 2;
            writer.BeginArray("tags", "tag", tagCount);
            for (int32_t tag = 0; tag < 2; ++tag) {
                writer.BeginArrayElement();
                writer.Write("t", id * 10 + tag);
                writer.EndArrayElement();
            }
            writer.EndArray();
        }

        /// What a reflected struct writes: scalars of every type, an object in an object, an array of elements with an object and an array each, an empty array
        Vector<uint8_t> PlainDocument() {
            BinarySerializer writer;
            writer.WriteAttribute("flag", true);
            writer.WriteAttribute("small", static_cast<int8_t>(-5));
            writer.WriteAttribute("ubyte", static_cast<uint8_t>(200));
            writer.WriteAttribute("short", static_cast<int16_t>(-300));
            writer.WriteAttribute("ushort", static_cast<uint16_t>(60000));
            writer.WriteAttribute("int", static_cast<int32_t>(-70000));
            writer.WriteAttribute("uint", static_cast<uint32_t>(4000000000u));
            writer.WriteAttribute("long", static_cast<int64_t>(-5000000000));
            writer.WriteAttribute("ulong", static_cast<uint64_t>(10000000000ull));
            writer.WriteAttribute("single", 1.25f);
            writer.WriteAttribute("double", 2.5);
            writer.Write("text", StringView("plain"));
            writer.BeginObject("nested");
            writer.Write("x", 1);
            writer.Write("label", StringView("n"));
            writer.BeginObject("deep");
            writer.Write("v", 7);
            writer.EndObject();
            writer.EndObject();
            size_t count = 5;
            writer.BeginArray("items", "item", count);
            for (int32_t i = 0; i < 5; ++i) {
                writer.BeginArrayElement();
                WriteElementFields(writer, i);
                writer.EndArrayElement();
            }
            writer.EndArray();
            count = 0;
            writer.BeginArray("none", "item", count);
            writer.EndArray();
            return writer.GetData();
        }

        /// A scope of @p fields scalars `f<k>`, then an object of the same
        Vector<uint8_t> WideDocument(size_t fields) {
            BinarySerializer writer;
            for (size_t k = 0; k < fields; ++k) {
                writer.Write(Format("f{}", k), static_cast<int32_t>(k));
            }
            writer.BeginObject("inner");
            for (size_t k = 0; k < fields; ++k) {
                writer.Write(Format("g{}", k), static_cast<float>(k));
            }
            writer.EndObject();
            return writer.GetData();
        }

    }  // namespace

    TEST(BinaryDeserializerTest, TheLookupFromTheLastMatchEqualsAScanOnAPlainDocument) {
        ExpectLookupMatchesScan(PlainDocument(), false, "plain");
    }

    TEST(BinaryDeserializerTest, TheLookupFromTheLastMatchEqualsAScanOnWideScopes) {
        ExpectLookupMatchesScan(WideDocument(40), false, "wide");
        ExpectLookupMatchesScan(WideDocument(kInlineKeyLimit + 1), false, "just over the inline keys");
    }

    TEST(BinaryDeserializerTest, ADocumentOfDistinctNamesHasNoDuplicateFlag) {
        const Vector<uint8_t> bytes = MakeStream(kSpriteSizedElements);
        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromView(bytes.data(), bytes.size()));
        EXPECT_FALSE(BinaryDeserializerAccess::HasDuplicateNames(deserializer));
    }

    // The generated Serialize writes each member once, so the fast lookup applies to every file the engine writes itself.
    TEST(BinaryDeserializerTest, TheStreamOfAGeneratedSerializeHasNoDuplicateFlag) {
        TextureLibrary library;
        for (size_t i = 0; i < kSpriteSizedElements; ++i) {
            SpriteEntry entry;
            entry.name = PoolString::Intern(Format("binary_deserializer_sprite_{}", i));
            entry.texturePath = PoolString::Intern(Format("assets/binary_deserializer/atlas_{}.png", i));
            entry.srcRect = Rect{static_cast<float>(i), 0.0f, 16.0f, 32.0f};
            library.AddOrUpdate(eastl::move(entry));
        }
        BinarySerializer writer;
        library.Write(writer);

        const Vector<uint8_t>& bytes = writer.GetData();
        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromView(bytes.data(), bytes.size()));
        EXPECT_FALSE(BinaryDeserializerAccess::HasDuplicateNames(deserializer));
    }

    TEST(BinaryDeserializerTest, AScalarWrittenTwiceReadsAsTheLastWrite) {
        BinarySerializer writer;
        writer.Write("before", 0);
        writer.Write("x", 1);
        writer.WriteAttribute("x", 2);
        writer.Write("after", 3);
        ExpectLookupMatchesScan(writer.GetData(), true, "scalar twice");

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromBuffer(writer.GetData()));
        int32_t value = 0;
        ASSERT_TRUE(reader.Read("x", value));
        EXPECT_EQ(value, 2);
    }

    TEST(BinaryDeserializerTest, ScalarsOfTwoTypesUnderOneNameResolveToTheLastOne) {
        BinarySerializer writer;
        writer.Write("x", 1);
        writer.Write("x", 2.5f);
        ExpectLookupMatchesScan(writer.GetData(), true, "int then float");

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromBuffer(writer.GetData()));
        float value = 0;
        ASSERT_TRUE(reader.Read("x", value));
        EXPECT_EQ(value, 2.5f);
        int32_t asInt = 0;
        EXPECT_FALSE(reader.ReadAttribute("x", asInt));
    }

    TEST(BinaryDeserializerTest, AnObjectAndAnArrayWrittenTwiceReadAsTheLastWrite) {
        BinarySerializer writer;
        for (int32_t round = 0; round < 2; ++round) {
            writer.BeginObject("o");
            writer.Write(round == 0 ? "first" : "second", round);
            writer.EndObject();
            size_t count = static_cast<size_t>(round + 1);
            writer.BeginArray("a", "item", count);
            for (int32_t i = 0; i <= round; ++i) {
                writer.BeginArrayElement();
                writer.Write("v", i + 10 * round);
                writer.EndArrayElement();
            }
            writer.EndArray();
        }
        const Vector<uint8_t> bytes = writer.GetData();
        ExpectLookupMatchesScan(bytes, true, "object and array twice");

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));
        ASSERT_TRUE(reader.BeginObject("o"));
        EXPECT_FALSE(reader.HasKey("first"));
        EXPECT_TRUE(reader.HasKey("second"));
        reader.EndObject();
        size_t count = 0;
        ASSERT_TRUE(reader.BeginArray("a", "item", count));
        EXPECT_EQ(count, 2u);
    }

    TEST(BinaryDeserializerTest, ANameOfTwoKindsIsNotARepeatAndBothStayReadable) {
        BinarySerializer writer;
        writer.WriteAttribute("x", 5);
        writer.BeginObject("x");
        writer.Write("inside", 6);
        writer.EndObject();
        size_t count = 1;
        writer.BeginArray("x", "item", count);
        writer.BeginArrayElement();
        writer.Write("v", 7);
        writer.EndArrayElement();
        writer.EndArray();
        const Vector<uint8_t> bytes = writer.GetData();
        ExpectLookupMatchesScan(bytes, false, "x as scalar, object and array");

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));
        int32_t value = 0;
        ASSERT_TRUE(reader.Read("x", value));
        EXPECT_EQ(value, 5);
        ASSERT_TRUE(reader.BeginObject("x"));
        ASSERT_TRUE(reader.Read("inside", value));
        EXPECT_EQ(value, 6);
        reader.EndObject();
        size_t elements = 0;
        EXPECT_TRUE(reader.BeginArray("x", "item", elements));
    }

    TEST(BinaryDeserializerTest, ARepeatInsideANestedScopeOrAnArrayElementMarksTheFile) {
        BinarySerializer inObject;
        inObject.BeginObject("outer");
        inObject.Write("a", 1);
        inObject.Write("a", 2);
        inObject.EndObject();
        ExpectLookupMatchesScan(inObject.GetData(), true, "repeat in an object");

        BinarySerializer inElement;
        size_t count = 3;
        inElement.BeginArray("items", "item", count);
        for (int32_t i = 0; i < 3; ++i) {
            inElement.BeginArrayElement();
            inElement.Write("v", i);
            if (i == 2) {
                inElement.Write("v", 100);
            }
            inElement.EndArrayElement();
        }
        inElement.EndArray();
        ExpectLookupMatchesScan(inElement.GetData(), true, "repeat in the last element");
    }

    TEST(BinaryDeserializerTest, ARepeatBeyondTheInlineKeysIsFound) {
        for (const size_t repeated : {kInlineKeyLimit - 1, kInlineKeyLimit, kInlineKeyLimit + 1, size_t{99}}) {
            BinarySerializer writer;
            for (size_t k = 0; k < 100; ++k) {
                writer.Write(Format("f{}", k), static_cast<int32_t>(k));
            }
            writer.Write(Format("f{}", repeated), -1);
            ExpectLookupMatchesScan(writer.GetData(), true, "repeat of a late name");
        }
        // A repeat of the first name found only after the keys spilled out of the stack array
        BinarySerializer first;
        first.Write("f0", 1);
        for (size_t k = 1; k < 3 * kInlineKeyLimit; ++k) {
            first.Write(Format("f{}", k), static_cast<int32_t>(k));
        }
        first.Write("f0", 2);
        ExpectLookupMatchesScan(first.GetData(), true, "repeat of the first name");
    }

    TEST(BinaryDeserializerTest, AScopeBeyondTheTrackedEntriesIsTakenAsRepeatingAndStillReadsTheLastWrite) {
        BinarySerializer writer;
        for (size_t k = 0; k < kMaxTrackedEntriesForTest + 1; ++k) {
            writer.Write(Format("n{}", k), static_cast<int32_t>(k));
        }
        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromBuffer(writer.GetData()));
        EXPECT_TRUE(BinaryDeserializerAccess::HasDuplicateNames(reader));
        int32_t value = -1;
        ASSERT_TRUE(reader.Read("n0", value));
        EXPECT_EQ(value, 0);
        ASSERT_TRUE(reader.Read(Format("n{}", kMaxTrackedEntriesForTest), value));
        EXPECT_EQ(value, static_cast<int32_t>(kMaxTrackedEntriesForTest));
    }

    TEST(BinaryDeserializerTest, HasKeyBeforeAReadFindsTheSameName) {
        const Vector<uint8_t> bytes = PlainDocument();
        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));
        for (const char* name : {"double", "flag", "text", "int", "flag"}) {
            ASSERT_TRUE(reader.HasKey(name)) << name;
        }
        double value = 0;
        EXPECT_TRUE(reader.Read("double", value));
        bool flag = false;
        EXPECT_TRUE(reader.Read("flag", flag));
        EXPECT_TRUE(flag);
    }

    TEST(BinaryDeserializerTest, AnArrayElementPastTheCountIsRefusedAndAnEmptyArrayHasNone) {
        const Vector<uint8_t> bytes = PlainDocument();
        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));

        size_t count = 99;
        ASSERT_TRUE(reader.BeginArray("none", "item", count));
        EXPECT_EQ(count, 0u);
        EXPECT_FALSE(reader.BeginArrayElement());
        reader.EndArray();

        ASSERT_TRUE(reader.BeginArray("items", "item", count));
        ASSERT_EQ(count, 5u);
        for (size_t i = 0; i < count; ++i) {
            ASSERT_TRUE(reader.BeginArrayElement());
            size_t tags = 0;
            ASSERT_TRUE(reader.BeginArray("tags", "tag", tags));
            EXPECT_EQ(tags, 2u);
            reader.EndArray();
            reader.EndArrayElement();
        }
        EXPECT_FALSE(reader.BeginArrayElement());
        reader.EndArray();
        EXPECT_FALSE(reader.BeginArrayElement());
    }

    // =============================================================================
    // Version 2: a string table, entries that refer to it by index
    // =============================================================================

    namespace {

        using BinaryStreams::Element;
        using BinaryStreams::Number;
        using BinaryStreams::Pooled;
        using BinaryStreams::Text;

        template <typename T>
        void ExpectNumberRead(BinaryDeserializer& reader, const BinaryStreams::Entry& entry, const String& where) {
            T value{};
            ASSERT_TRUE(reader.Read(entry.name, value)) << where.c_str();
            T expected{};
            std::memcpy(&expected, &entry.bits, sizeof(T));
            EXPECT_EQ(std::memcmp(&value, &expected, sizeof(T)), 0) << where.c_str();
        }

        /// Reads every entry of @p entries by name and compares it with the tree the stream was built from (names are distinct in the tree)
        void ExpectReadsEntries(BinaryDeserializer& reader, const Vector<BinaryStreams::Entry>& entries, const String& where) {
            for (const BinaryStreams::Entry& entry : entries) {
                const String here = Format("{}/{}", where, entry.name);
                switch (entry.tag) {
                    case BinaryFormat::Tag::Bool:
                        ExpectNumberRead<bool>(reader, entry, here);
                        break;
                    case BinaryFormat::Tag::Int8:
                        ExpectNumberRead<int8_t>(reader, entry, here);
                        break;
                    case BinaryFormat::Tag::UInt8:
                        ExpectNumberRead<uint8_t>(reader, entry, here);
                        break;
                    case BinaryFormat::Tag::Int16:
                        ExpectNumberRead<int16_t>(reader, entry, here);
                        break;
                    case BinaryFormat::Tag::UInt16:
                        ExpectNumberRead<uint16_t>(reader, entry, here);
                        break;
                    case BinaryFormat::Tag::Int32:
                        ExpectNumberRead<int32_t>(reader, entry, here);
                        break;
                    case BinaryFormat::Tag::UInt32:
                        ExpectNumberRead<uint32_t>(reader, entry, here);
                        break;
                    case BinaryFormat::Tag::Int64:
                        ExpectNumberRead<int64_t>(reader, entry, here);
                        break;
                    case BinaryFormat::Tag::UInt64:
                        ExpectNumberRead<uint64_t>(reader, entry, here);
                        break;
                    case BinaryFormat::Tag::Float:
                        ExpectNumberRead<float>(reader, entry, here);
                        break;
                    case BinaryFormat::Tag::Double:
                        ExpectNumberRead<double>(reader, entry, here);
                        break;
                    case BinaryFormat::Tag::String:
                    case BinaryFormat::Tag::PoolString: {
                        String asString;
                        ASSERT_TRUE(reader.Read(entry.name, asString)) << here.c_str();
                        EXPECT_EQ(asString, entry.text) << here.c_str();
                        PoolString asPool;
                        ASSERT_TRUE(reader.Read(entry.name, asPool)) << here.c_str();
                        EXPECT_EQ(String(asPool.ToStringView()), entry.text) << here.c_str();
                        break;
                    }
                    case BinaryFormat::Tag::Object:
                        ASSERT_TRUE(reader.BeginObject(entry.name)) << here.c_str();
                        ExpectReadsEntries(reader, entry.children, here);
                        reader.EndObject();
                        break;
                    default: {
                        size_t count = 0;
                        ASSERT_TRUE(reader.BeginArray(entry.name, "item", count)) << here.c_str();
                        ASSERT_EQ(count, entry.children.size()) << here.c_str();
                        for (size_t i = 0; i < count; ++i) {
                            ASSERT_TRUE(reader.BeginArrayElement()) << here.c_str();
                            ExpectReadsEntries(reader, entry.children[i].children, Format("{}[{}]", here, i));
                            reader.EndArrayElement();
                        }
                        reader.EndArray();
                        break;
                    }
                }
            }
        }

        /// A document of every kind of entry, with PoolString values (one repeated) and a name that is also a value
        Vector<BinaryStreams::Entry> SampleTree() {
            Vector<BinaryStreams::Entry> items;
            for (int32_t i = 0; i < 3; ++i) {
                items.push_back(Element({Number("id", i), Pooled("kind", i == 1 ? "square" : "circle"), Text("label", Format("label_{}", i)),
                                         BinaryStreams::Object("pos", {Number("x", 0.5f * static_cast<float>(i)), Number("y", 1.5f)})}));
            }
            return {Number("flag", true),
                    Number("small", static_cast<int8_t>(-5)),
                    Number("ubyte", static_cast<uint8_t>(200)),
                    Number("short", static_cast<int16_t>(-300)),
                    Number("ushort", static_cast<uint16_t>(60000)),
                    Number("int", static_cast<int32_t>(-70000)),
                    Number("uint", static_cast<uint32_t>(4000000000u)),
                    Number("long", static_cast<int64_t>(-5000000000)),
                    Number("ulong", static_cast<uint64_t>(10000000000ull)),
                    Number("single", 1.25f),
                    Number("double", 2.5),
                    Text("text", "plain"),
                    Pooled("square", "circle"),
                    BinaryStreams::Object("nested", {Number("x", 1), Pooled("tag", "deep"), BinaryStreams::Object("deep", {Number("v", 7)})}),
                    BinaryStreams::Array("items", items),
                    BinaryStreams::Array("none", {})};
        }

        BinaryStreams::Options Version(uint16_t version) {
            BinaryStreams::Options options;
            options.version = version;
            return options;
        }

        /// The versions with a string table: a case about behaviour runs over each
        constexpr uint16_t kTabledVersions[] = {BinaryFormat::TabledVersion, BinaryFormat::PooledVersion};

        /// A stream whose payload size is right for its length, so the load reaches the check under test
        Vector<uint8_t> Resized(Vector<uint8_t> bytes, size_t size) {
            bytes.resize(size);
            BinaryFormat::Store<uint32_t>(bytes.data() + BinaryFormat::PayloadSizeOffset, static_cast<uint32_t>(size - BinaryFormat::HeaderSize));
            return bytes;
        }

        void ExpectRefused(const Vector<uint8_t>& bytes, const char* what) {
            BinaryDeserializer fromView;
            EXPECT_FALSE(fromView.LoadFromView(bytes.data(), bytes.size())) << what;
            EXPECT_TRUE(fromView.HasErrors()) << what;
            EXPECT_FALSE(fromView.HasKey("a")) << what;
            EXPECT_FALSE(fromView.BeginObject("o")) << what;
            int32_t value = 0;
            EXPECT_FALSE(fromView.Read("a", value)) << what;

            BinaryDeserializer fromBuffer;
            EXPECT_FALSE(fromBuffer.LoadFromBuffer(bytes)) << what;
        }

        String LastError(const BinaryDeserializer& reader) {
            return reader.GetErrors().empty() ? String() : reader.GetErrors().back().errorMessage;
        }

    }  // namespace

    TEST(BinaryDeserializerTest, ADocumentReadsEqualInEveryVersion) {
        const Vector<BinaryStreams::Entry> tree = SampleTree();
        for (const uint16_t version : {BinaryFormat::LegacyVersion, BinaryFormat::TabledVersion, BinaryFormat::PooledVersion}) {
            const Vector<uint8_t> bytes = BinaryStreams::Build(tree, Version(version));
            BinaryDeserializer reader;
            ASSERT_TRUE(reader.LoadFromBuffer(bytes)) << version;
            ExpectReadsEntries(reader, tree, Format("version {}", version));
            EXPECT_FALSE(reader.HasErrors()) << version;
            EXPECT_FALSE(BinaryDeserializerAccess::HasDuplicateNames(reader)) << version;
        }
    }

    TEST(BinaryDeserializerTest, ATabledStreamLoadsFromAViewABufferAndAFile) {
        for (const uint16_t version : kTabledVersions) {
            SCOPED_TRACE(Format("version {}", version).c_str());
            const Vector<BinaryStreams::Entry> tree = SampleTree();
            const Vector<uint8_t> bytes = BinaryStreams::Build(tree, Version(version));

            BinaryDeserializer fromView;
            ASSERT_TRUE(fromView.LoadFromView(bytes.data(), bytes.size()));
            ExpectReadsEntries(fromView, tree, "view");

            BinaryDeserializer fromBuffer;
            ASSERT_TRUE(fromBuffer.LoadFromBuffer(bytes));
            ExpectReadsEntries(fromBuffer, tree, "buffer");

            const std::filesystem::path path = TestTempPath(Format("binary_deserializer_v{}_file", version));
            WriteFile(path, StringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
            BinaryDeserializer fromFile;
            const bool opened = fromFile.LoadFromFile(path);
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            ASSERT_TRUE(opened);
            ExpectReadsEntries(fromFile, tree, "file");
        }
    }

    TEST(BinaryDeserializerTest, AMovedReaderOfATabledStreamStillReads) {
        for (const uint16_t version : kTabledVersions) {
            SCOPED_TRACE(Format("version {}", version).c_str());
            const Vector<BinaryStreams::Entry> tree = SampleTree();
            const Vector<uint8_t> bytes = BinaryStreams::Build(tree, Version(version));

            BinaryDeserializer owning;
            ASSERT_TRUE(owning.LoadFromBuffer(bytes));
            BinaryDeserializer movedOwning(eastl::move(owning));
            ExpectReadsEntries(movedOwning, tree, "moved owning reader");

            BinaryDeserializer viewing;
            ASSERT_TRUE(viewing.LoadFromView(bytes.data(), bytes.size()));
            BinaryDeserializer movedViewing;
            movedViewing = eastl::move(viewing);
            ExpectReadsEntries(movedViewing, tree, "moved view");

            // The moved reader keeps the strings it interned for the first PoolString read
            BinaryDeserializer first;
            ASSERT_TRUE(first.LoadFromBuffer(bytes));
            PoolString kind;
            ASSERT_TRUE(first.Read("square", kind));
            BinaryDeserializer movedAfterRead(eastl::move(first));
            ExpectReadsEntries(movedAfterRead, tree, "moved after a PoolString read");
        }
    }

    TEST(BinaryDeserializerTest, ANameThatIsNotInTheTableIsNotFound) {
        for (const uint16_t version : kTabledVersions) {
            SCOPED_TRACE(Format("version {}", version).c_str());
            const Vector<uint8_t> bytes = BinaryStreams::Build({Number("a", 1), BinaryStreams::Object("o", {Number("b", 2)})}, Version(version));
            BinaryDeserializer reader;
            ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));

            int32_t value = 5;
            EXPECT_FALSE(reader.HasKey("zzz"));
            EXPECT_FALSE(reader.ReadAttribute("zzz", value));
            EXPECT_FALSE(reader.HasErrors());
            EXPECT_FALSE(reader.Read("zzz", value));
            EXPECT_EQ(LastError(reader), "Element not found");
            EXPECT_EQ(value, 5);

            // In the table, but not in this scope
            reader.ClearErrors();
            ASSERT_TRUE(reader.BeginObject("o"));
            EXPECT_FALSE(reader.HasKey("a"));
            EXPECT_FALSE(reader.ReadAttribute("a", value));
            EXPECT_FALSE(reader.HasErrors());
            EXPECT_TRUE(reader.Read("b", value));
            EXPECT_EQ(value, 2);
        }
    }

    TEST(BinaryDeserializerTest, ACorruptStringTableIsRefusedAndNothingIsReadable) {
        const Vector<BinaryStreams::Entry> tree = {Number("a", 1), BinaryStreams::Object("o", {Number("bb", 2)}), Pooled("c", "bb")};
        const Vector<uint8_t> good = BinaryStreams::Build(tree, Version(BinaryFormat::TabledVersion));
        const BinaryStreams::Layout layout = BinaryStreams::LayoutOf(good);
        ASSERT_EQ(BinaryFormat::Load<uint32_t>(good.data() + layout.countOffset), 4u);  // a, bb, c, o

        BinaryStreams::Options unsorted;
        unsorted.table = {"bb", "a", "c", "o"};
        ExpectRefused(BinaryStreams::Build(tree, unsorted), "unsorted");

        BinaryStreams::Options repeated;
        repeated.table = {"a", "bb", "bb", "c", "o"};
        ExpectRefused(BinaryStreams::Build(tree, repeated), "a text twice");

        Vector<uint8_t> decreasing = good;
        BinaryFormat::Store<uint32_t>(decreasing.data() + layout.endsOffset + sizeof(uint32_t), 0);  // end[1] below end[0]
        ExpectRefused(decreasing, "end[] decreasing");

        Vector<uint8_t> lastEnd = good;
        const size_t lastEndAt = layout.endsOffset + 3 * sizeof(uint32_t);
        BinaryFormat::Store<uint32_t>(lastEnd.data() + lastEndAt, BinaryFormat::Load<uint32_t>(lastEnd.data() + lastEndAt) - 1);
        ExpectRefused(lastEnd, "last end is not charBytes");

        Vector<uint8_t> countTooLarge = good;
        BinaryFormat::Store<uint32_t>(countTooLarge.data() + layout.countOffset, 0xFFFFFFFFu);
        ExpectRefused(countTooLarge, "count larger than the bytes");

        Vector<uint8_t> charsTooLarge = good;
        BinaryFormat::Store<uint32_t>(charsTooLarge.data() + layout.charBytesOffset, 0x7FFFFFFFu);
        ExpectRefused(charsTooLarge, "charBytes over the stream");

        Vector<uint8_t> nameOutOfRange = good;
        BinaryFormat::Store<uint32_t>(nameOutOfRange.data() + layout.entriesOffset + sizeof(uint8_t), 4);  // the first entry's name index = count
        ExpectRefused(nameOutOfRange, "name index at count");

        // The third entry of the root is the PoolString: tag, name index, string index
        BinaryFormat::StringTable table;
        size_t tableEnd = 0;
        ASSERT_TRUE(BinaryFormat::ParseStringTable(good.data(), BinaryFormat::HeaderSize, good.size(), table, tableEnd));
        size_t poolEntry = tableEnd;
        for (size_t i = 0; i < 2; ++i) {
            BinaryFormat::EntryView skipped;
            ASSERT_TRUE(BinaryFormat::ParseEntryV2(good.data(), poolEntry, good.size(), table, skipped));
            poolEntry = skipped.end;
        }
        Vector<uint8_t> stringOutOfRange = good;
        BinaryFormat::Store<uint32_t>(stringOutOfRange.data() + poolEntry + sizeof(uint8_t) + sizeof(uint32_t), 4);
        ExpectRefused(stringOutOfRange, "tag 15 index at count");

        ExpectRefused(Resized(good, layout.charsOffset - 1), "truncated inside end[]");
        ExpectRefused(Resized(good, layout.entriesOffset - 1), "truncated inside the characters");
        ExpectRefused(Resized(good, BinaryFormat::HeaderSize), "header only: no table");
        ExpectRefused(Resized(good, BinaryFormat::HeaderSize + sizeof(uint32_t)), "half a table header");

        Vector<uint8_t> unknownVersion = good;
        BinaryFormat::Store<uint16_t>(unknownVersion.data() + sizeof(BinaryFormat::Magic), BinaryFormat::PooledVersion + 1);
        ExpectRefused(unknownVersion, "a version above the newest");

        Vector<uint8_t> tagInVersion1 = BinaryStreams::Build({Text("a", "x")}, Version(BinaryFormat::LegacyVersion));
        ASSERT_EQ(tagInVersion1[BinaryFormat::HeaderSize], BinaryFormat::Tag::String);
        tagInVersion1[BinaryFormat::HeaderSize] = BinaryFormat::Tag::PoolString;
        ExpectRefused(tagInVersion1, "tag 15 in version 1");
    }

    TEST(BinaryDeserializerTest, AnEmptyTableIsALoadableDocument) {
        for (const uint16_t version : kTabledVersions) {
            SCOPED_TRACE(Format("version {}", version).c_str());
            const Vector<uint8_t> bytes = BinaryStreams::Build({}, Version(version));
            const size_t sectionBytes = version == BinaryFormat::PooledVersion ? BinaryFormat::PoolSectionHeadSize : 0;
            ASSERT_EQ(bytes.size(), BinaryFormat::HeaderSize + 2 * sizeof(uint32_t) + sectionBytes);
            BinaryDeserializer reader;
            ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));
            EXPECT_FALSE(reader.HasKey("a"));
            EXPECT_FALSE(reader.HasErrors());
        }
    }

    TEST(BinaryDeserializerTest, ATableThatIsNotSortedByBytesIsRefusedWhateverTheCharSignedness) {
        for (const uint16_t version : kTabledVersions) {
            SCOPED_TRACE(Format("version {}", version).c_str());
            // 0x80 is below 'a' as a signed char and above it as the unsigned byte the format orders by
            const String high = "\x80";
            BinaryStreams::Options options = Version(version);
            options.table = {"a", high};
            EXPECT_TRUE(BinaryFormat::CompareStrings("a", high) < 0);
            const Vector<BinaryStreams::Entry> tree = {Number("a", 1), Number(high, 2)};
            BinaryDeserializer reader;
            const Vector<uint8_t> ordered = BinaryStreams::Build(tree, options);
            ASSERT_TRUE(reader.LoadFromView(ordered.data(), ordered.size()));
            int32_t value = 0;
            ASSERT_TRUE(reader.Read(high, value));
            EXPECT_EQ(value, 2);

            options.table = {high, "a"};
            const Vector<uint8_t> reversed = BinaryStreams::Build(tree, options);
            ExpectRefused(reversed, "high byte first");
        }
    }

    // =============================================================================
    // What a load puts into the pool
    // =============================================================================

    TEST(BinaryDeserializerTest, OnlyAPoolStringValueIsInternedNotANameNorAStringValue) {
        for (const uint16_t version : kTabledVersions) {
            SCOPED_TRACE(Format("version {}", version).c_str());
            const String name = FreshText("bin_name");
            const String stringName = FreshText("bin_string_name");
            const String stringValue = FreshText("bin_string_value");
            const String poolName = FreshText("bin_pool_name");
            const String poolValue = FreshText("bin_pool_value");
            const Vector<uint8_t> bytes = BinaryStreams::Build({Number(name, 1), Text(stringName, stringValue), Pooled(poolName, poolValue)}, Version(version));

            BinaryDeserializer reader;
            ASSERT_TRUE(reader.LoadFromBuffer(bytes));
            int32_t number = 0;
            String text;
            PoolString pooled;
            ASSERT_TRUE(reader.Read(name, number));
            ASSERT_TRUE(reader.Read(stringName, text));
            EXPECT_EQ(text, stringValue);
            ASSERT_TRUE(reader.Read(poolName, pooled));

            EXPECT_FALSE(PoolString::Find(StringView(name)).has_value());
            EXPECT_FALSE(PoolString::Find(StringView(stringName)).has_value());
            EXPECT_FALSE(PoolString::Find(StringView(stringValue)).has_value());
            EXPECT_FALSE(PoolString::Find(StringView(poolName)).has_value());
            ASSERT_TRUE(PoolString::Find(StringView(poolValue)).has_value());
            EXPECT_EQ(*PoolString::Find(StringView(poolValue)), pooled);
        }
    }

    TEST(BinaryDeserializerTest, ThePoolStringsOfAFileAreInternedInOneBatchAtTheFirstPoolStringRead) {
        for (const uint16_t version : kTabledVersions) {
            SCOPED_TRACE(Format("version {}", version).c_str());
            Vector<BinaryStreams::Entry> tree;
            Vector<String> values;
            for (size_t i = 0; i < 20; ++i) {
                values.push_back(FreshText("bin_batch_value"));
            }
            // The first five entries share one text, so the table holds fewer PoolStrings than the file has entries
            const auto valueOf = [&](size_t i) -> const String& { return i < 5 ? values[0] : values[i]; };
            for (size_t i = 0; i < values.size(); ++i) {
                tree.push_back(Pooled(Format("p{}", i), valueOf(i)));
            }
            tree.push_back(Number("n", 7));
            tree.push_back(Text("s", "some text"));
            const Vector<uint8_t> bytes = BinaryStreams::Build(tree, Version(version));

            const uint64_t before = PoolString::GetStats().batchCalls;
            BinaryDeserializer reader;
            ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));
            int32_t number = 0;
            String text;
            ASSERT_TRUE(reader.Read("n", number));
            ASSERT_TRUE(reader.Read("s", text));
            EXPECT_EQ(PoolString::GetStats().batchCalls, before) << "a load and the other fields intern nothing";

            PoolString first;
            ASSERT_TRUE(reader.Read("p0", first));
            EXPECT_EQ(PoolString::GetStats().batchCalls, before + 1);
            for (size_t i = 0; i < values.size(); ++i) {
                PoolString value;
                ASSERT_TRUE(i % 2 == 0 ? reader.Read(Format("p{}", i), value) : reader.ReadAttribute(Format("p{}", i), value));
                EXPECT_EQ(String(value.ToStringView()), valueOf(i));
                EXPECT_EQ(value, *PoolString::Find(StringView(valueOf(i))));
            }
            EXPECT_EQ(PoolString::GetStats().batchCalls, before + 1) << "later reads take the result of the batch";
        }
    }

    TEST(BinaryDeserializerTest, AFileOfVersion1InternsEachPoolStringWhenItIsRead) {
        const String value = FreshText("bin_v1_value");
        const Vector<uint8_t> bytes = BinaryStreams::Build({Pooled("p", value)}, Version(BinaryFormat::LegacyVersion));
        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));
        EXPECT_FALSE(PoolString::Find(StringView(value)).has_value());

        const uint64_t before = PoolString::GetStats().batchCalls;
        PoolString pooled;
        ASSERT_TRUE(reader.Read("p", pooled));
        EXPECT_EQ(String(pooled.ToStringView()), value);
        EXPECT_TRUE(PoolString::Find(StringView(value)).has_value());
        EXPECT_EQ(PoolString::GetStats().batchCalls, before);
    }

    // Version 2 only: a version 3 pool section never holds a text over the limit (the writer puts it inline), and the stream is refused when it does
    TEST(BinaryDeserializerTest, APoolStringOverTheLimitIsAnErrorAndStaysOutOfThePool) {
        const String tooLong = FreshText("bin_long") + String(ReaderLimits::kMaxPoolStringBytes, 'x');
        const String fine = FreshText("bin_fine");
        const Vector<uint8_t> bytes = BinaryStreams::Build({Pooled("long", tooLong), Pooled("fine", fine), Text("inline", tooLong)}, Version(BinaryFormat::TabledVersion));

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromBuffer(bytes));
        const String keptText = FreshText("bin_kept");
        const PoolString before = PoolString::Intern(StringView(keptText));
        PoolString kept = before;
        EXPECT_FALSE(reader.Read("long", kept));
        EXPECT_TRUE(reader.HasErrors());
        EXPECT_EQ(kept, before);

        PoolString readFine;
        ASSERT_TRUE(reader.Read("fine", readFine));
        EXPECT_EQ(String(readFine.ToStringView()), fine);
        EXPECT_FALSE(reader.Read("inline", kept));

        // A String field has no such limit
        String asString;
        ASSERT_TRUE(reader.Read("long", asString));
        EXPECT_EQ(asString, tooLong);
        EXPECT_FALSE(PoolString::Find(StringView(tooLong)).has_value());
    }

    TEST(BinaryDeserializerTest, StringAndPoolStringEntriesReadAsEitherAndOtherTypesMismatch) {
        for (const uint16_t version : kTabledVersions) {
            SCOPED_TRACE(Format("version {}", version).c_str());
            const Vector<uint8_t> bytes = BinaryStreams::Build({Pooled("pooled", "from the table"), Text("inline", "from the entry"), Number("number", 3)}, Version(version));
            BinaryDeserializer reader;
            ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));

            String asString;
            ASSERT_TRUE(reader.Read("pooled", asString));
            EXPECT_EQ(asString, "from the table");
            PoolString asPool;
            ASSERT_TRUE(reader.Read("inline", asPool));
            EXPECT_EQ(String(asPool.ToStringView()), "from the entry");

            EXPECT_FALSE(reader.Read("number", asString));
            EXPECT_EQ(LastError(reader), "Type mismatch");
            reader.ClearErrors();
            EXPECT_FALSE(reader.ReadAttribute("number", asPool));
            EXPECT_EQ(LastError(reader), "Type mismatch");
            reader.ClearErrors();
            int32_t number = 0;
            EXPECT_FALSE(reader.Read("pooled", number));
            EXPECT_EQ(LastError(reader), "Type mismatch");
            EXPECT_TRUE(reader.Read("number", number));
        }
    }

    // =============================================================================
    // Version 3: the pool section
    // =============================================================================

    namespace {

        /// A version 3 stream and where its parts start, found with the format helpers
        struct PooledStream {
            Vector<uint8_t> bytes;
            BinaryFormat::PoolSection section;
            size_t sectionEnd = 0;  ///< Where the first entry of the document starts
        };

        PooledStream BuildPooled(const Vector<BinaryStreams::Entry>& tree, const BinaryStreams::Options& options = Version(BinaryFormat::PooledVersion)) {
            PooledStream built;
            built.bytes = BinaryStreams::Build(tree, options);
            BinaryFormat::StringTable names;
            size_t namesEnd = 0;
            if (!BinaryFormat::ParseStringTable(built.bytes.data(), BinaryFormat::HeaderSize, built.bytes.size(), names, namesEnd) ||
                !BinaryFormat::ParsePoolSection(built.bytes.data(), namesEnd, built.bytes.size(), built.section, built.sectionEnd)) {
                ADD_FAILURE() << "the stream built for the test has no good pool section";
            }
            return built;
        }

        constexpr const char* kPoolSectionMessage = "Binary pool section is truncated or corrupt";

        void ExpectPoolSectionRefused(const Vector<uint8_t>& bytes, const char* what) {
            BinaryDeserializer reader;
            EXPECT_FALSE(reader.LoadFromView(bytes.data(), bytes.size())) << what;
            EXPECT_EQ(LastError(reader), kPoolSectionMessage) << what;
            EXPECT_FALSE(reader.HasKey("a")) << what;
        }

    }  // namespace

    TEST(BinaryDeserializerTest, AStaleOrWrongHashAndChecksumInTheSectionStillReadTheRightPoolStrings) {
        const String stale = FreshText("bin_stale");
        const String other = FreshText("bin_other");
        BinaryStreams::Options options = Version(BinaryFormat::PooledVersion);
        options.hashOverrides = {{stale, StringUtils::GetHash(stale) ^ 0x5555}, {other, 7}};
        options.hashFunction = 0x7777;
        options.checksum = 1;
        const PooledStream built = BuildPooled({Pooled("a", stale), Pooled("b", other), Pooled("c", stale)}, options);

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromView(built.bytes.data(), built.bytes.size()));
        PoolString a;
        PoolString b;
        PoolString c;
        ASSERT_TRUE(reader.Read("a", a));
        ASSERT_TRUE(reader.Read("b", b));
        ASSERT_TRUE(reader.Read("c", c));
        EXPECT_EQ(a, c);
        // A text put where its wrong hash points would not be found by the hash of the text
        EXPECT_EQ(PoolString::Find(StringView(stale)), a);
        EXPECT_EQ(PoolString::Find(StringView(other)), b);
        EXPECT_EQ(String(a.ToStringView()), stale);
        EXPECT_EQ(a.HashValue(), StringUtils::GetHash(stale));
        EXPECT_EQ(b.HashValue(), StringUtils::GetHash(other));
    }

    TEST(BinaryDeserializerTest, ARepeatedTextAndAnUnsortedSectionReadRight) {
        const String first = FreshText("bin_first");
        const String second = FreshText("bin_second");
        BinaryStreams::Options options = Version(BinaryFormat::PooledVersion);
        options.pool = {second, first, second, ""};
        const PooledStream built = BuildPooled({Pooled("a", first), Pooled("b", second), Pooled("e", "")}, options);
        ASSERT_EQ(built.section.count, 4u);

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromView(built.bytes.data(), built.bytes.size()));
        PoolString a;
        PoolString b;
        PoolString empty = PoolString::Intern(StringView(FreshText("bin_not_empty")));
        ASSERT_TRUE(reader.Read("a", a));
        ASSERT_TRUE(reader.Read("b", b));
        ASSERT_TRUE(reader.Read("e", empty));
        EXPECT_EQ(String(a.ToStringView()), first);
        EXPECT_EQ(String(b.ToStringView()), second);
        EXPECT_EQ(empty, PoolString());
        EXPECT_EQ(PoolString::Find(StringView(second)), b);
    }

    TEST(BinaryDeserializerTest, APoolStringReadIntoAStringInternsNothing) {
        const String value = FreshText("bin_to_string");
        const PooledStream built = BuildPooled({Pooled("p", value), Number("n", 3)});

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromView(built.bytes.data(), built.bytes.size()));
        const uint64_t strings = PoolString::GetStats().stringCount;
        int32_t number = 0;
        ASSERT_TRUE(reader.Read("n", number));
        EXPECT_TRUE(reader.HasKey("p"));
        String text;
        ASSERT_TRUE(reader.Read("p", text));
        ASSERT_TRUE(reader.ReadAttribute("p", text));
        EXPECT_EQ(text, value);
        EXPECT_EQ(PoolString::GetStats().stringCount, strings);
        EXPECT_FALSE(PoolString::Find(StringView(value)).has_value());
    }

    TEST(BinaryDeserializerTest, ACorruptPoolSectionIsRefusedWithOneMessage) {
        const Vector<BinaryStreams::Entry> tree = {Pooled("a", "first text"), Pooled("b", "second")};
        const PooledStream built = BuildPooled(tree);
        const Vector<uint8_t>& good = built.bytes;
        const size_t head = built.section.headOffset;
        const size_t firstEntry = built.section.entriesOffset;
        ASSERT_EQ(built.section.count, 2u);

        Vector<uint8_t> reserved = good;
        BinaryFormat::Store<uint32_t>(reserved.data() + head + 3 * sizeof(uint32_t), 1);
        ExpectPoolSectionRefused(reserved, "reserved is not 0");

        Vector<uint8_t> unaligned = good;
        BinaryFormat::Store<uint32_t>(unaligned.data() + head + sizeof(uint32_t), built.section.bytes - 1);
        ExpectPoolSectionRefused(unaligned, "bytes is not a multiple of 8");

        Vector<uint8_t> tooMany = good;
        BinaryFormat::Store<uint32_t>(tooMany.data() + head + sizeof(uint32_t), 0x7FFFFFF8u);
        ExpectPoolSectionRefused(tooMany, "bytes past the stream");

        Vector<uint8_t> wrongCount = good;
        BinaryFormat::Store<uint32_t>(wrongCount.data() + head, 3);
        ExpectPoolSectionRefused(wrongCount, "count of three");

        Vector<uint8_t> sizeRuns = good;
        BinaryFormat::Store<uint32_t>(sizeRuns.data() + firstEntry + offsetof(Details::PoolStringEntry, size), 4000);
        ExpectPoolSectionRefused(sizeRuns, "a size past the section");

        Vector<uint8_t> noNul = good;
        noNul[firstEntry + offsetof(Details::PoolStringEntry, data) + 10] = 'x';  // "first text" is ten bytes
        ExpectPoolSectionRefused(noNul, "no NUL after the text");

        BinaryStreams::Options tooLong = Version(BinaryFormat::PooledVersion);
        tooLong.pool = {String(ReaderLimits::kMaxPoolStringBytes + 1, 'x'), "first text", "second"};
        ExpectPoolSectionRefused(BinaryStreams::Build(tree, tooLong), "a text over the limit");

        ExpectPoolSectionRefused(Resized(good, built.section.headOffset + BinaryFormat::PoolSectionHeadSize - 1), "a stream cut inside the head");
        ExpectPoolSectionRefused(Resized(good, firstEntry + 20), "a stream cut inside the entries");
    }

    TEST(BinaryDeserializerTest, APoolStringIndexAtOrPastTheSectionCountIsRefused) {
        const PooledStream built = BuildPooled({Pooled("a", "one"), Pooled("b", "two")});
        Vector<uint8_t> atCount = built.bytes;
        // The first entry of the document: tag, name index, string index
        BinaryFormat::Store<uint32_t>(atCount.data() + built.sectionEnd + sizeof(uint8_t) + sizeof(uint32_t), built.section.count);
        BinaryDeserializer reader;
        EXPECT_FALSE(reader.LoadFromView(atCount.data(), atCount.size()));
        EXPECT_TRUE(reader.HasErrors());
        EXPECT_FALSE(reader.HasKey("a"));
    }

    TEST(BinaryDeserializerTest, ASectionOfVersion3IsInternedByOneBatchAtTheFirstPoolStringRead) {
        const String value = FreshText("bin_v3_batch");
        const String unreferenced = FreshText("bin_v3_unreferenced");
        BinaryStreams::Options options = Version(BinaryFormat::PooledVersion);
        options.pool = {value, unreferenced};
        const PooledStream built = BuildPooled({Pooled("p", value), Number("n", 1)}, options);

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromView(built.bytes.data(), built.bytes.size()));
        int32_t number = 0;
        ASSERT_TRUE(reader.Read("n", number));
        EXPECT_FALSE(PoolString::Find(StringView(value)).has_value()) << "a load and the other fields intern nothing";

        const uint64_t batches = PoolString::GetStats().batchCalls;
        PoolString pooled;
        ASSERT_TRUE(reader.Read("p", pooled));
        EXPECT_EQ(PoolString::GetStats().batchCalls, batches + 1);
        ASSERT_TRUE(reader.Read("p", pooled));
        EXPECT_EQ(PoolString::GetStats().batchCalls, batches + 1);
        EXPECT_EQ(PoolString::Find(StringView(value)), pooled);
    }

}  // namespace BECore::Tests
