#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <Common/AllocCheck.h>
#include <Common/BinaryStreams.h>
#include <Common/FreshText.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kEmptyStreamSize = BinaryFormat::HeaderSize + 2 * sizeof(uint32_t) + BinaryFormat::PoolSectionHeadSize;
        /// Elements written before the allocation check and in it; the check is on the names, not on the growth of the buffer
        constexpr size_t kWarmUpElements = 64;
        constexpr size_t kCountedElements = 200;
        constexpr uint64_t kBufferGrowthBlocks = 16;

        struct ParsedTable {
            BinaryFormat::StringTable table;
            Vector<String> texts;
            size_t end = 0;  ///< Where the table ends: the pool section starts here
        };

        /// The name table of @p bytes parsed with the format helpers, not with the reader (whose lookup is what the table is for)
        ParsedTable ParseTable(const Vector<uint8_t>& bytes) {
            ParsedTable parsed;
            EXPECT_TRUE(BinaryFormat::ParseStringTable(bytes.data(), BinaryFormat::HeaderSize, bytes.size(), parsed.table, parsed.end));
            for (uint32_t i = 0; i < parsed.table.count; ++i) {
                parsed.texts.push_back(String(parsed.table.Get(i)));
            }
            return parsed;
        }

        struct ParsedPool {
            BinaryFormat::PoolSection section;
            size_t end = 0;  ///< Where the section ends: the entries start here
            Vector<String> texts;
            Vector<uint64_t> hashes;
        };

        /// The pool section of @p bytes, parsed the same way
        ParsedPool ParsePool(const Vector<uint8_t>& bytes) {
            ParsedPool parsed;
            EXPECT_TRUE(BinaryFormat::ParsePoolSection(bytes.data(), ParseTable(bytes).end, bytes.size(), parsed.section, parsed.end));
            size_t at = parsed.section.entriesOffset;
            for (uint32_t i = 0; i < parsed.section.count; ++i) {
                const size_t size = BinaryFormat::Load<uint32_t>(bytes.data() + at + offsetof(Details::PoolStringEntry, size));
                parsed.texts.emplace_back(reinterpret_cast<const char*>(bytes.data() + at + offsetof(Details::PoolStringEntry, data)), size);
                parsed.hashes.push_back(BinaryFormat::Load<uint64_t>(bytes.data() + at + offsetof(Details::PoolStringEntry, hash)));
                at += Details::PoolEntryBytes(size);
            }
            return parsed;
        }

        Vector<String> Texts(std::initializer_list<StringView> texts) {
            Vector<String> result;
            for (const StringView text : texts) {
                result.push_back(String(text));
            }
            return result;
        }

        /// An element with a number, a `PoolString` and a `String`
        void WriteElement(BinarySerializer& writer, int32_t id, const PoolString& color) {
            if (writer.BeginArrayElement()) {
                writer.WriteAttribute("id", id);
                writer.WriteAttribute("color", color);
                writer.WriteAttribute("label", StringView("inline label"));
                writer.EndArrayElement();
            }
        }

    }  // namespace

    TEST(BinarySerializerTest, BothTablesAreSortedAndHaveEachTextOnce) {
        BinarySerializer writer;
        size_t count = 3;
        if (writer.BeginArray("items", "item", count)) {
            WriteElement(writer, 1, "red"_intern);
            WriteElement(writer, 2, "blue"_intern);
            WriteElement(writer, 3, "red"_intern);
            writer.EndArray();
        }

        const Vector<uint8_t>& bytes = writer.GetData();
        EXPECT_EQ(ParseTable(bytes).texts, Texts({"color", "id", "items", "label"}));
        EXPECT_EQ(ParsePool(bytes).texts, Texts({"blue", "red"}));
    }

    TEST(BinarySerializerTest, ATextThatIsANameAndAPoolStringValueIsInBothTables) {
        BinarySerializer writer;
        writer.Write("title", "title"_intern);
        writer.Write("other", "title"_intern);

        const Vector<uint8_t>& bytes = writer.GetData();
        EXPECT_EQ(ParseTable(bytes).texts, Texts({"other", "title"}));
        EXPECT_EQ(ParsePool(bytes).texts, Texts({"title"}));
    }

    TEST(BinarySerializerTest, APoolStringValueIsInThePoolSectionAndAStringValueIsNot) {
        BinarySerializer writer;
        writer.Write("s", StringView("inline_text_not_in_the_section"));
        writer.Write("p", "pooled_text_in_the_section"_intern);
        writer.Write("e", PoolString());

        const Vector<uint8_t>& bytes = writer.GetData();
        EXPECT_EQ(ParseTable(bytes).texts, Texts({"e", "p", "s"}));
        EXPECT_EQ(ParsePool(bytes).texts, Texts({"", "pooled_text_in_the_section"}));

        const Vector<BinaryStreams::Entry> tree = BinaryStreams::ReadTree(bytes);
        ASSERT_EQ(tree.size(), 3u);
        EXPECT_EQ(tree[0].tag, BinaryFormat::Tag::String);
        EXPECT_EQ(tree[0].text, "inline_text_not_in_the_section");
        EXPECT_EQ(tree[1].tag, BinaryFormat::Tag::PoolString);
        EXPECT_EQ(tree[1].text, "pooled_text_in_the_section");
        EXPECT_EQ(tree[2].tag, BinaryFormat::Tag::PoolString);
        EXPECT_EQ(tree[2].text, "");

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromBuffer(bytes));
        PoolString emptied = "stale"_intern;
        EXPECT_TRUE(reader.Read("e", emptied));
        EXPECT_TRUE(emptied.Empty());
    }

    TEST(BinarySerializerTest, TheSectionHoldsTheHashOfEachPoolStringAndTheChecksumOfItsBytes) {
        BinarySerializer writer;
        writer.Write("a", "alpha"_intern);
        writer.Write("b", "h\xC3\xA9llo"_intern);
        writer.Write("c", "gamma"_intern);

        const Vector<uint8_t>& bytes = writer.GetData();
        const ParsedPool pool = ParsePool(bytes);
        ASSERT_EQ(pool.texts.size(), 3u);
        for (size_t i = 0; i < pool.texts.size(); ++i) {
            EXPECT_EQ(pool.hashes[i], PoolString::Intern(StringView(pool.texts[i])).HashValue()) << pool.texts[i].c_str();
            EXPECT_EQ(pool.hashes[i], StringUtils::GetHash(StringView(pool.texts[i]))) << pool.texts[i].c_str();
        }
        EXPECT_EQ(pool.section.hashFunction, Details::PoolHashFunctionId());
        EXPECT_EQ(pool.section.checksum, BinaryFormat::PoolSectionChecksum(bytes.data() + pool.section.entriesOffset, pool.section.bytes));
    }

    TEST(BinarySerializerTest, PoolSectionStatsAndForEachPoolTextDescribeTheSection) {
        BinarySerializer writer;
        EXPECT_EQ(writer.PoolSectionStats().count, 0u);
        EXPECT_EQ(writer.PoolSectionStats().bytes, 0u);

        writer.Write("a", "alpha"_intern);
        writer.Write("b", "alpha"_intern);
        writer.Write("c", "beta_longer"_intern);
        writer.Write("d", StringView("inline"));

        const ParsedPool pool = ParsePool(writer.GetData());
        EXPECT_EQ(writer.PoolSectionStats().count, pool.section.count);
        EXPECT_EQ(writer.PoolSectionStats().bytes, pool.section.bytes);

        Vector<String> visited;
        writer.ForEachPoolText([&visited](StringView text) { visited.push_back(String(text)); });
        eastl::sort(visited.begin(), visited.end());
        EXPECT_EQ(visited, Texts({"alpha", "beta_longer"}));
    }

    TEST(BinarySerializerTest, APoolStringOverTheLimitIsWrittenInlineAndKeptOutOfTheSection) {
        const String tooLong = FreshText("ser_long") + String(ReaderLimits::kMaxPoolStringBytes, 'x');
        ASSERT_GT(tooLong.size(), ReaderLimits::kMaxPoolStringBytes);
        BinarySerializer writer;
        writer.Write("long", PoolString::Intern(StringView(tooLong)));
        writer.Write("fine", "fits_in_the_pool"_intern);

        const Vector<uint8_t>& bytes = writer.GetData();
        EXPECT_EQ(ParsePool(bytes).texts, Texts({"fits_in_the_pool"}));
        const Vector<BinaryStreams::Entry> tree = BinaryStreams::ReadTree(bytes);
        ASSERT_EQ(tree.size(), 2u);
        EXPECT_EQ(tree[0].tag, BinaryFormat::Tag::String);
        EXPECT_EQ(tree[0].text, tooLong);

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromBuffer(bytes));
        PoolString kept = "stale"_intern;
        EXPECT_FALSE(reader.Read("long", kept));
        EXPECT_TRUE(reader.HasErrors());
        EXPECT_EQ(kept, "stale"_intern);
        String text;
        EXPECT_TRUE(reader.Read("long", text));
        EXPECT_EQ(text, tooLong);
    }

    TEST(BinarySerializerTest, WriteAndWriteAttributeMakeTheSameEntries) {
        BinarySerializer written;
        written.Write("n", int32_t{7});
        written.Write("s", StringView("text"));
        written.Write("p", "pool"_intern);

        BinarySerializer attributes;
        attributes.WriteAttribute("n", int32_t{7});
        attributes.WriteAttribute("s", StringView("text"));
        attributes.WriteAttribute("p", "pool"_intern);

        EXPECT_EQ(written.GetData(), attributes.GetData());
    }

    TEST(BinarySerializerTest, EntriesHoldTheSortedIndicesNotTheOrderOfFirstUse) {
        BinarySerializer writer;
        writer.Write("z", "a"_intern);

        // clang-format off
        Vector<uint8_t> expected = {
            'B', 'E', 'B', 'N', 0x03, 0x00,                                                 // magic, version 3
            0x3E, 0x00, 0x00, 0x00,                                                         // payload size: 62
            0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,                                 // name table: 1 name, 1 byte of characters
            0x01, 0x00, 0x00, 0x00,                                                         // name end
            'z',                                                                            // 0 "z"
            0x01, 0x00, 0x00, 0x00,                                                         // pool section: 1 entry
            0x10, 0x00, 0x00, 0x00,                                                         //   16 bytes
            0x00, 0x00, 0x00, 0x00,                                                         //   hash function (stored below)
            0x00, 0x00, 0x00, 0x00,                                                         //   reserved
            0xB3, 0x41, 0xA2, 0xE0, 0xFE, 0x58, 0x75, 0x9D,                                 //   checksum
            0x8C, 0xEC, 0x01, 0x86, 0x4C, 0xDC, 0x63, 0xAF,                                 //   entry 0: the FNV-1a 64 hash of "a"
            0x01, 0x00, 0x00, 0x00, 'a', 0x00, 0x00, 0x00,                                  //   size 1, "a", NUL, padding
            0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                           // PoolString "z" = pool entry 0
        };
        // clang-format on
        // The id names the hash and the signedness of `char`, so it is the program's own
        BinaryFormat::Store<uint32_t>(expected.data() + BinaryFormat::HeaderSize + 13 + 2 * sizeof(uint32_t), Details::PoolHashFunctionId());
        EXPECT_EQ(writer.GetData(), expected);
    }

    TEST(BinarySerializerTest, TheSameShapeInAnotherFirstUseOrderGivesTheSameTables) {
        BinarySerializer forward;
        forward.Write("a", "x"_intern);
        forward.Write("b", "y"_intern);
        forward.Write("c", "z"_intern);

        BinarySerializer backward;
        backward.Write("c", "z"_intern);
        backward.Write("b", "y"_intern);
        backward.Write("a", "x"_intern);

        const Vector<uint8_t>& first = forward.GetData();
        const Vector<uint8_t>& second = backward.GetData();
        const size_t tablesBytes = ParsePool(first).end;
        ASSERT_EQ(tablesBytes, ParsePool(second).end);
        EXPECT_TRUE(eastl::equal(first.begin(), first.begin() + tablesBytes, second.begin()));
    }

    TEST(BinarySerializerTest, GetDataAfterMoreWritesHasTheNewEntries) {
        BinarySerializer writer;
        writer.Write("first", int32_t{1});

        const Vector<uint8_t> before = writer.GetData();
        writer.Write("second", "later"_intern);
        const Vector<uint8_t>& after = writer.GetData();
        EXPECT_NE(before, after);

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromBuffer(after));
        int32_t first = 0;
        PoolString second;
        EXPECT_TRUE(reader.Read("first", first));
        EXPECT_TRUE(reader.Read("second", second));
        EXPECT_EQ(first, 1);
        EXPECT_EQ(second, "later"_intern);
    }

    TEST(BinarySerializerTest, GetDataWithoutAWriteInBetweenReturnsTheSameBytes) {
        BinarySerializer writer;
        writer.Write("a", int32_t{1});

        const Vector<uint8_t>& first = writer.GetData();
        const Vector<uint8_t> copy = first;
        const Vector<uint8_t>& second = writer.GetData();
        EXPECT_EQ(second, copy);
        EXPECT_EQ(&first, &second);
    }

    TEST(BinarySerializerTest, ClosingAScopeAfterAGetDataIsInTheNextImage) {
        BinarySerializer writer;
        ASSERT_TRUE(writer.BeginObject("o"));
        writer.Write("a", int32_t{1});
        const Vector<uint8_t> whileOpen = writer.GetData();
        writer.EndObject();

        // EndObject only patches the size of the scope: that alone makes the earlier image stale
        EXPECT_NE(writer.GetData(), whileOpen);
        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromBuffer(writer.GetData()));
        int32_t value = 0;
        ASSERT_TRUE(reader.BeginObject("o"));
        EXPECT_TRUE(reader.Read("a", value));
        EXPECT_EQ(value, 1);
    }

    TEST(BinarySerializerTest, ASerializerThatWroteNothingIsAnEmptyLoadableStream) {
        const BinarySerializer writer;
        EXPECT_EQ(writer.GetData().size(), kEmptyStreamSize);

        BinaryDeserializer reader;
        EXPECT_TRUE(reader.LoadFromBuffer(writer.GetData()));
        EXPECT_FALSE(reader.BeginObject("anything"));
        EXPECT_FALSE(reader.HasErrors());
    }

    TEST(BinarySerializerTest, ANameThatIsAlreadyInTheTableAllocatesNothingForTheName) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }

        BinarySerializer writer;
        size_t count = kWarmUpElements + kCountedElements;
        ASSERT_TRUE(writer.BeginArray("items", "item", count));
        const PoolString color = "red"_intern;
        for (size_t i = 0; i < kWarmUpElements; ++i) {
            WriteElement(writer, static_cast<int32_t>(i), color);
        }

        const AllocCount counted = CountAllocations([&] {
            for (size_t i = 0; i < kCountedElements; ++i) {
                WriteElement(writer, static_cast<int32_t>(i), color);
            }
        });
        writer.EndArray();

        // Four texts an element, 200 elements: a name that cost an allocation would show as hundreds
        EXPECT_LT(counted.count, kBufferGrowthBlocks);
    }

}  // namespace BECore::Tests
