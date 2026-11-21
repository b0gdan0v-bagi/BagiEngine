#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <Common/AllocCheck.h>
#include <Common/TempPath.h>
#include <filesystem>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kSpriteSizedElements = 2000;
        constexpr uint64_t kBoundedLoadBlocks = 8;
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

}  // namespace BECore::Tests
