// The heap cost of the names JsonDeserializer keeps for its unknown-key findings: they sit in the reader's own arena.

#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
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

        /// {"key0":0, ..., "obj0":{"key0":0, ...}, ...}: 10 root keys and 4 objects of 5 keys, 30 asked names in all
        Json MakeTree() {
            Json root = Json::object();
            for (size_t i = 0; i < kRootKeys; ++i) {
                root[KeyName(i).c_str()] = static_cast<int32_t>(i);
            }
            for (size_t object = 0; object < kNestedObjects; ++object) {
                Json child = Json::object();
                for (size_t i = 0; i < kNestedKeys; ++i) {
                    child[KeyName(i).c_str()] = static_cast<int32_t>(i);
                }
                root[Format("obj{}", object).c_str()] = std::move(child);
            }
            return root;
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
        const Json document = MakeTree();
        {
            JsonDeserializer warmUp(document);
            ReadTree(warmUp);
        }
        JsonDeserializer reader(document);
        reader.SetDiagnostics(DiagnosticsMode::Warn);

        const AllocCount counted = CountAllocations([&] { ReadTree(reader); });

        EXPECT_TRUE(reader.GetWarnings().empty());
        EXPECT_LE(counted.count, kHeapBlocks);
    }

    TEST(JsonScratchTest, ADeserializerWithDiagnosticsOffAllocatesNothingForNames) {
        const Json document = MakeTree();
        JsonDeserializer reader(document);

        ReadTree(reader);  // the first read pays for what the formatter and the JSON lookup set up once

        ExpectNoAllocations([&] { ReadTree(reader); });
    }

    TEST(JsonScratchTest, ANearMissNextToAnAskedKeyStillGetsItsHint) {
        Json root = MakeTree();
        root["key1x"] = 1;
        JsonDeserializer reader(root);
        reader.SetDiagnostics(DiagnosticsMode::Warn);

        ReadTree(reader);

        ASSERT_EQ(reader.GetWarnings().size(), 1u);
        EXPECT_EQ(reader.GetWarnings()[0].errorMessage, "Unknown key 'key1x', did you mean 'key1'?");
    }

}  // namespace BECore::Tests
