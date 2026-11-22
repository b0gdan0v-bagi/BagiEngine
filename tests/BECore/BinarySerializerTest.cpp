#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <Common/AllocCheck.h>
#include <Common/BinaryStreams.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kEmptyStreamSize = BinaryFormat::HeaderSize + 2 * sizeof(uint32_t);
        /// Elements written before the allocation check and in it; the check is on the names, not on the growth of the buffer
        constexpr size_t kWarmUpElements = 64;
        constexpr size_t kCountedElements = 200;
        constexpr uint64_t kBufferGrowthBlocks = 16;

        struct ParsedTable {
            BinaryFormat::StringTable table;
            Vector<String> texts;
        };

        /// The table of @p bytes parsed with the format helpers, not with the reader (whose lookup is what the table is for)
        ParsedTable ParseTable(const Vector<uint8_t>& bytes) {
            ParsedTable parsed;
            size_t tableEnd = 0;
            EXPECT_TRUE(BinaryFormat::ParseStringTable(bytes.data(), BinaryFormat::HeaderSize, bytes.size(), parsed.table, tableEnd));
            for (uint32_t i = 0; i < parsed.table.count; ++i) {
                parsed.texts.push_back(String(parsed.table.Get(i)));
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

    TEST(BinarySerializerTest, TheTableIsSortedAndHasEachTextOnce) {
        BinarySerializer writer;
        size_t count = 3;
        if (writer.BeginArray("items", "item", count)) {
            WriteElement(writer, 1, "red"_intern);
            WriteElement(writer, 2, "blue"_intern);
            WriteElement(writer, 3, "red"_intern);
            writer.EndArray();
        }

        const ParsedTable parsed = ParseTable(writer.GetData());
        EXPECT_EQ(parsed.texts, Texts({"blue", "color", "id", "items", "label", "red"}));
    }

    TEST(BinarySerializerTest, ANameThatIsAlsoAValueIsOneEntry) {
        BinarySerializer writer;
        writer.Write("title", "title"_intern);
        writer.Write("other", "title"_intern);

        EXPECT_EQ(ParseTable(writer.GetData()).texts, Texts({"other", "title"}));
    }

    TEST(BinarySerializerTest, APoolStringValueIsTabledAndAStringValueIsNot) {
        BinarySerializer writer;
        writer.Write("s", StringView("inline_text_not_in_the_table"));
        writer.Write("p", "pooled_text_in_the_table"_intern);
        writer.Write("e", PoolString());

        const Vector<uint8_t>& bytes = writer.GetData();
        EXPECT_EQ(ParseTable(bytes).texts, Texts({"", "e", "p", "pooled_text_in_the_table", "s"}));

        const Vector<BinaryStreams::Entry> tree = BinaryStreams::ReadTree(bytes);
        ASSERT_EQ(tree.size(), 3u);
        EXPECT_EQ(tree[0].tag, BinaryFormat::Tag::String);
        EXPECT_EQ(tree[0].text, "inline_text_not_in_the_table");
        EXPECT_EQ(tree[1].tag, BinaryFormat::Tag::PoolString);
        EXPECT_EQ(tree[1].text, "pooled_text_in_the_table");
        EXPECT_EQ(tree[2].tag, BinaryFormat::Tag::PoolString);
        EXPECT_EQ(tree[2].text, "");
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

    TEST(BinarySerializerTest, EntriesHoldTheSortedIndexNotTheOrderOfFirstUse) {
        BinarySerializer writer;
        writer.Write("z", "a"_intern);

        // clang-format off
        const Vector<uint8_t> expected = {
            'B', 'E', 'B', 'N', 0x02, 0x00,                                                 // magic, version 2
            0x1B, 0x00, 0x00, 0x00,                                                         // payload size: 27
            0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,                                 // table: 2 strings, 2 bytes of characters
            0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,                                 // string ends
            'a', 'z',                                                                       // 0 "a", 1 "z"
            0x0F, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                           // PoolString "z" = string 0
        };
        // clang-format on
        EXPECT_EQ(writer.GetData(), expected);
    }

    TEST(BinarySerializerTest, TheSameShapeInAnotherFirstUseOrderGivesTheSameTable) {
        BinarySerializer forward;
        forward.Write("a", int32_t{1});
        forward.Write("b", int32_t{2});
        forward.Write("c", int32_t{3});

        BinarySerializer backward;
        backward.Write("c", int32_t{3});
        backward.Write("b", int32_t{2});
        backward.Write("a", int32_t{1});

        const Vector<uint8_t>& first = forward.GetData();
        const Vector<uint8_t>& second = backward.GetData();
        const size_t tableBytes = BinaryStreams::LayoutOf(first).entriesOffset;
        ASSERT_EQ(tableBytes, BinaryStreams::LayoutOf(second).entriesOffset);
        EXPECT_TRUE(eastl::equal(first.begin(), first.begin() + tableBytes, second.begin()));
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
