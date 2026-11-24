// The heap cost of the names JsonDeserializer keeps for its unknown-key findings: they sit in the reader's own arena.

#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonWriter.h>
#include <Common/AllocCheck.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kRootKeys = 10;
        constexpr size_t kNestedObjects = 4;
        constexpr size_t kNestedKeys = 5;
        /// What reading the tree below allocates with diagnostics on: the reader's arena and its chunk, once for the whole tree. The names
        /// were a Vector<String> per scope before (23 allocations: the growth of every list)
        constexpr uint64_t kHeapBlocks = 4;

        String KeyName(size_t index) {
            return Format("key{}", index);
        }

        /// {"key0":0, ..., "obj0":{"key0":0, ...}, ...}: 10 root keys and 4 objects of 5 keys, 30 asked names in all; @p extra is one more root key
        String MakeTree(StringView extra = {}) {
            String text;
            JsonWriter writer(text);
            writer.BeginObject();
            for (size_t i = 0; i < kRootKeys; ++i) {
                writer.Member(KeyName(i), static_cast<int32_t>(i));
            }
            for (size_t object = 0; object < kNestedObjects; ++object) {
                writer.Key(Format("obj{}", object));
                writer.BeginObject();
                for (size_t i = 0; i < kNestedKeys; ++i) {
                    writer.Member(KeyName(i), static_cast<int32_t>(i));
                }
                writer.EndObject();
            }
            if (!extra.empty()) {
                writer.Member(extra, 1);
            }
            writer.EndObject();
            return text;
        }

        void ReadTree(JsonDeserializer& reader) {
            int32_t value = 0;
            for (size_t i = 0; i < kRootKeys; ++i) {
                reader.Read(KeyName(i), value);
            }
            for (size_t object = 0; object < kNestedObjects; ++object) {
                if (reader.BeginObject(Format("obj{}", object))) {
                    for (size_t i = 0; i < kNestedKeys; ++i) {
                        reader.Read(KeyName(i), value);
                    }
                    reader.EndObject();
                }
            }
            reader.FinishLoad();
        }

    }  // namespace

    TEST(JsonScratchTest, ReadingATreeWithDiagnosticsOnKeepsItsNamesInOneArena) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        const String document = MakeTree();
        {
            JsonDeserializer warmUp;
            ASSERT_TRUE(warmUp.Load(document));
            ReadTree(warmUp);
        }
        JsonDeserializer reader;
        ASSERT_TRUE(reader.Load(document));
        reader.SetDiagnostics(DiagnosticsMode::Warn);

        const AllocCount counted = CountAllocations([&] { ReadTree(reader); });

        EXPECT_TRUE(reader.GetWarnings().empty());
        EXPECT_LE(counted.count, kHeapBlocks);
    }

    TEST(JsonScratchTest, ADeserializerWithDiagnosticsOffAllocatesNothingForNames) {
        const String document = MakeTree();
        JsonDeserializer reader;
        ASSERT_TRUE(reader.Load(document));

        ReadTree(reader);  // the first read pays for what the formatter and the JSON lookup set up once

        ExpectNoAllocations([&] { ReadTree(reader); });
    }

    TEST(JsonScratchTest, ANearMissNextToAnAskedKeyStillGetsItsHint) {
        const String document = MakeTree("key1x");
        JsonDeserializer reader;
        ASSERT_TRUE(reader.Load(document));
        reader.SetDiagnostics(DiagnosticsMode::Warn);

        ReadTree(reader);

        ASSERT_EQ(reader.GetWarnings().size(), 1u);
        EXPECT_EQ(reader.GetWarnings()[0].errorMessage, "Unknown key 'key1x', did you mean 'key1'?");
    }

}  // namespace BECore::Tests
