#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Resource/TextureLibrary.h>
#include <Common/AllocCheck.h>
#include <Common/TempPath.h>
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
        BinarySerializer BuildDocument(size_t elements, StringView text = kText) {
            BinarySerializer serializer;
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
            return serializer;
        }

        Vector<uint8_t> MakeStream(size_t elements, StringView text = kText) {
            return BuildDocument(elements, text).GetData();
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
        const bool saved = BuildDocument(50).SaveToFile(path);

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
        const bool saved = BuildDocument(0, "DDDD").SaveToFile(path);

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

        Vector<BinaryFormat::EntryView> EntriesOf(const Vector<uint8_t>& bytes, Range range) {
            Vector<BinaryFormat::EntryView> entries;
            size_t position = range.begin;
            while (position < range.end) {
                BinaryFormat::EntryView entry;
                if (!BinaryFormat::ParseEntry(bytes.data(), position, range.end, entry)) {
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

        void ExpectScalarRead(BinaryDeserializer& reader, const BinaryFormat::EntryView& entry, const uint8_t* data, const String& where) {
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
                    EXPECT_EQ(StringView(text), StringView(reinterpret_cast<const char*>(data + entry.payloadBegin + sizeof(uint32_t)), length)) << where.c_str();
                }
            }
        }

        void WalkScope(BinaryDeserializer& reader, const Vector<uint8_t>& bytes, Range range, ReadOrder order, const String& where);

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
        void WalkScope(BinaryDeserializer& reader, const Vector<uint8_t>& bytes, Range range, ReadOrder order, const String& where) {
            const Vector<BinaryFormat::EntryView> entries = EntriesOf(bytes, range);
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
                    ExpectScalarRead(reader, *expected, bytes.data(), here);
                } else if (item.kind == NameKind::Object) {
                    ASSERT_TRUE(reader.BeginObject(item.name)) << here.c_str();
                    WalkScope(reader, bytes, Range{expected->payloadBegin + sizeof(uint32_t), expected->end}, order, here);
                    reader.EndObject();
                } else {
                    size_t count = 0;
                    ASSERT_TRUE(reader.BeginArray(item.name, "item", count)) << here.c_str();
                    const size_t countOffset = expected->payloadBegin + sizeof(uint32_t);
                    ASSERT_EQ(count, BinaryFormat::Load<uint32_t>(bytes.data() + countOffset)) << here.c_str();
                    size_t position = countOffset + sizeof(uint32_t);
                    for (size_t i = 0; i < count; ++i) {
                        const size_t size = BinaryFormat::Load<uint32_t>(bytes.data() + position);
                        ASSERT_TRUE(reader.BeginArrayElement()) << here.c_str();
                        WalkScope(reader, bytes, Range{position + sizeof(uint32_t), position + sizeof(uint32_t) + size}, order, Format("{}[{}]", here, i));
                        reader.EndArrayElement();
                        position += sizeof(uint32_t) + size;
                    }
                    EXPECT_FALSE(reader.BeginArrayElement()) << here.c_str();
                    reader.EndArray();
                }
            }
        }

        /// Loads @p bytes once per order and walks the whole document against the scan
        void ExpectLookupMatchesScan(const Vector<uint8_t>& bytes, bool expectDuplicateFlag, const char* what) {
            for (const ReadOrder order : {ReadOrder::Written, ReadOrder::Reversed, ReadOrder::Shuffled, ReadOrder::EachTwice, ReadOrder::EveryOther}) {
                BinaryDeserializer reader;
                ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size())) << what;
                EXPECT_EQ(BinaryDeserializerAccess::HasDuplicateNames(reader), expectDuplicateFlag) << what;
                WalkScope(reader, bytes, Range{BinaryFormat::HeaderSize, bytes.size()}, order, what);
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

}  // namespace BECore::Tests
