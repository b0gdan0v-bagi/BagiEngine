#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <Common/AllocCheck.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kWideEntries = 300;  // over the validation's inline key count
        constexpr size_t kPooledEntries = 120;
        constexpr int32_t kBase = 7;
        // Bounds with a little room above what the reader allocates now (the scope stack, the pooled lists): the validation and the intern list
        // took 5 and 18 allocations before they moved to scratch
        constexpr uint64_t kWideLoadAllocations = 2;
        constexpr uint64_t kFirstPoolReadAllocations = 12;
        // The writer of 300 entries took 330 allocations, a node per distinct text among them; now the arena chunk, the entry buffer's growth and the image
        constexpr uint64_t kWideWriteAllocations = 12;

        String NameOf(size_t index) {
            return Format("entry_{}", index);
        }

        /// A document `doc` with @p entries integers in one scope
        Vector<uint8_t> WideDocument() {
            BinarySerializer serializer;
            serializer.BeginObject("doc");
            for (size_t i = 0; i < kWideEntries; ++i) {
                serializer.Write(NameOf(i), kBase + static_cast<int32_t>(i));
            }
            serializer.EndObject();
            return serializer.GetData();
        }

        /// A document `doc` with PoolString values, each text a different one
        Vector<uint8_t> PooledDocument() {
            BinarySerializer serializer;
            serializer.BeginObject("doc");
            for (size_t i = 0; i < kPooledEntries; ++i) {
                serializer.Write(NameOf(i), PoolString::Intern(Format("binary_scratch_text_{}", i)));
            }
            serializer.EndObject();
            return serializer.GetData();
        }

        /// The document of WideDocument() written by a serializer that is given a name from a buffer it overwrites afterwards
        const Vector<uint8_t>& WriteWithReusedNameBuffer(BinarySerializer& serializer) {
            serializer.BeginObject("doc");
            String name;
            for (size_t i = 0; i < kWideEntries; ++i) {
                name = NameOf(i);
                serializer.Write(name, kBase + static_cast<int32_t>(i));
                name.assign(name.size(), '#');
            }
            serializer.EndObject();
            return serializer.GetData();
        }

    }  // namespace

    static_assert(!std::is_move_constructible_v<BinarySerializer> && !std::is_move_assignable_v<BinarySerializer>, "the writer owns an arena its containers point into");

    TEST(BinaryScratchTest, AWideScopeIsReadBackAndValidatedOnScratch) {
        const Vector<uint8_t> bytes = WideDocument();
        {
            BinaryDeserializer warm;
            ASSERT_TRUE(warm.LoadFromView(bytes.data(), bytes.size()));  // gives the thread's scratch arena its chunk
        }

        BinaryDeserializer reader;
        bool loaded = false;
        const AllocCount count = CountAllocations([&] { loaded = reader.LoadFromView(bytes.data(), bytes.size()); });

        ASSERT_TRUE(loaded);
        ASSERT_TRUE(reader.BeginObject("doc"));
        int32_t value = 0;
        for (size_t i = 0; i < kWideEntries; ++i) {
            ASSERT_TRUE(reader.Read(NameOf(i), value));
            EXPECT_EQ(value, kBase + static_cast<int32_t>(i));
        }
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_LE(count.count, kWideLoadAllocations) << count.bytes << " bytes";
        } else {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

    TEST(BinaryScratchTest, TheFirstPoolStringReadInternsOnScratchAndReadsTheSame) {
        const Vector<uint8_t> bytes = PooledDocument();
        {
            BinaryDeserializer warm;
            ASSERT_TRUE(warm.LoadFromView(bytes.data(), bytes.size()));
            ASSERT_TRUE(warm.BeginObject("doc"));
            PoolString ignored;
            ASSERT_TRUE(warm.Read(NameOf(0), ignored));  // interns the texts and gives the scratch arena its chunk
        }

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));
        ASSERT_TRUE(reader.BeginObject("doc"));
        PoolString first;
        bool read = false;
        const AllocCount count = CountAllocations([&] { read = reader.Read(NameOf(0), first); });

        ASSERT_TRUE(read);
        EXPECT_EQ(first, "binary_scratch_text_0"_intern);
        for (size_t i = 1; i < kPooledEntries; ++i) {
            PoolString text;
            ASSERT_TRUE(reader.Read(NameOf(i), text));
            EXPECT_EQ(text, PoolString::Intern(Format("binary_scratch_text_{}", i)));
        }
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_LE(count.count, kFirstPoolReadAllocations) << count.bytes << " bytes";
        } else {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

    TEST(BinaryScratchTest, AWideDocumentIsWrittenOnTheWritersArena) {
        Vector<String> names;
        for (size_t i = 0; i < kWideEntries; ++i) {
            names.push_back(NameOf(i));
        }
        {
            BinarySerializer warm;
            (void)WriteWithReusedNameBuffer(warm);
        }

        Vector<uint8_t> written;
        const AllocCount count = CountAllocations([&] {
            BinarySerializer serializer;
            serializer.BeginObject("doc");
            for (size_t i = 0; i < kWideEntries; ++i) {
                serializer.Write(names[i], kBase + static_cast<int32_t>(i));
            }
            serializer.EndObject();
            written = serializer.GetData();
        });

        EXPECT_EQ(written, WideDocument());
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_LE(count.count, kWideWriteAllocations) << count.bytes << " bytes";
        } else {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

    TEST(BinaryScratchTest, TheTableKeepsTheTextOfANameThatWasATemporary) {
        BinarySerializer reused;
        const Vector<uint8_t>& bytes = WriteWithReusedNameBuffer(reused);
        EXPECT_EQ(bytes, WideDocument());

        BinaryDeserializer reader;
        ASSERT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));
        ASSERT_TRUE(reader.BeginObject("doc"));
        int32_t value = 0;
        for (size_t i = 0; i < kWideEntries; ++i) {
            ASSERT_TRUE(reader.Read(NameOf(i), value));
            EXPECT_EQ(value, kBase + static_cast<int32_t>(i));
        }
    }

    TEST(BinaryScratchTest, ASerializerThatWroteNothingGivesALoadableImage) {
        const BinarySerializer serializer;
        const Vector<uint8_t>& bytes = serializer.GetData();

        BinaryDeserializer reader;
        EXPECT_TRUE(reader.LoadFromView(bytes.data(), bytes.size()));
        EXPECT_FALSE(reader.BeginObject("doc"));
    }

}  // namespace BECore::Tests
