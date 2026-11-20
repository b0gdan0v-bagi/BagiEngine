#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Memory/EngineAllocator.h>
#include <BECore/Memory/MemTag.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <utility>

#if defined(__SANITIZE_ADDRESS__)
#define BE_TEST_UNDER_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define BE_TEST_UNDER_ASAN 1
#endif
#endif

namespace BECore::Tests {

    namespace {

        constexpr size_t kAlignment = 64;
        constexpr size_t kGrowthSteps = 100;
        constexpr int kCount = 200;
        constexpr size_t kInlineCapacity = 4;
        constexpr size_t kInlineStringCapacity = 8;

        struct alignas(kAlignment) Wide {
            char bytes[kAlignment];
        };

    }  // namespace

    TEST(EngineAllocatorTest, AllInstancesAreEqual) {
        EXPECT_TRUE(EngineAllocator{} == EngineAllocator{"named"});
        EXPECT_FALSE(EngineAllocator{} != EngineAllocator{"named"});
    }

    TEST(EngineAllocatorTest, AVectorGrowsCopiesMovesAndSwaps) {
        Vector<int> values;
        for (int i = 0; i < kCount; ++i) {
            values.push_back(i);
        }
        ASSERT_EQ(values.size(), static_cast<size_t>(kCount));
        EXPECT_EQ(values[kCount - 1], kCount - 1);

        Vector<int> copy = values;
        EXPECT_EQ(copy, values);

        const int* storage = values.data();
        Vector<int> moved = std::move(values);
        EXPECT_EQ(moved.data(), storage);

        Vector<int> other{1, 2, 3};
        moved.swap(other);
        EXPECT_EQ(moved.size(), 3u);
        EXPECT_EQ(other.data(), storage);
    }

    TEST(EngineAllocatorTest, AStringLongerThanItsInlineBufferGrowsCopiesAndMoves) {
        String text;
        text.append(kCount, 'x');
        EXPECT_EQ(text.size(), static_cast<size_t>(kCount));

        String copy = text;
        EXPECT_EQ(copy, text);

        String moved = std::move(text);
        EXPECT_EQ(moved.size(), static_cast<size_t>(kCount));
        EXPECT_EQ(moved[kCount - 1], 'x');

        String other{"short"};
        moved.swap(other);
        EXPECT_EQ(moved, "short");
        EXPECT_EQ(other.size(), static_cast<size_t>(kCount));
    }

    TEST(EngineAllocatorTest, ANodeContainerGrowsCopiesMovesAndSwaps) {
        HashMap<int, int> map;
        Map<int, int> tree;
        for (int i = 0; i < kCount; ++i) {
            map[i] = i * 2;
            tree[i] = i * 3;
        }
        EXPECT_EQ(map.size(), static_cast<size_t>(kCount));
        EXPECT_EQ(map.at(kCount - 1), (kCount - 1) * 2);
        EXPECT_EQ(tree.at(kCount - 1), (kCount - 1) * 3);

        HashMap<int, int> mapCopy = map;
        Map<int, int> treeCopy = tree;
        EXPECT_EQ(mapCopy.size(), map.size());
        EXPECT_EQ(treeCopy, tree);

        HashMap<int, int> movedMap = std::move(map);
        Map<int, int> movedTree = std::move(tree);
        HashMap<int, int> emptyMap;
        movedMap.swap(emptyMap);
        EXPECT_TRUE(movedMap.empty());
        EXPECT_EQ(emptyMap.size(), static_cast<size_t>(kCount));
        EXPECT_EQ(movedTree.size(), static_cast<size_t>(kCount));
    }

    TEST(EngineAllocatorTest, AFixedVectorTakesItsOverflowFromTheAllocator) {
        SmallVector<int, kInlineCapacity> values;
        for (int i = 0; i < kCount; ++i) {
            values.push_back(i);
        }
        EXPECT_TRUE(values.has_overflowed());
        EXPECT_EQ(values.size(), static_cast<size_t>(kCount));
        EXPECT_EQ(values[kCount - 1], kCount - 1);

        auto copy = values;
        EXPECT_EQ(copy, values);
        auto moved = std::move(values);
        EXPECT_EQ(moved.size(), static_cast<size_t>(kCount));
    }

    TEST(EngineAllocatorTest, AFixedStringTakesItsOverflowFromTheAllocator) {
        FixedString<kInlineStringCapacity> text;
        text.append(kCount, 'y');
        EXPECT_TRUE(text.has_overflowed());
        EXPECT_EQ(text.size(), static_cast<size_t>(kCount));

        auto copy = text;
        EXPECT_EQ(copy, text);
    }

    TEST(EngineAllocatorTest, AnOverAlignedElementStaysAlignedThroughGrowth) {
#if defined(BE_TEST_UNDER_ASAN)
        GTEST_SKIP() << "ASan reports the aligned new / plain delete pair of EASTL";
#else
        Vector<Wide> wide;
        for (size_t i = 0; i < kGrowthSteps; ++i) {
            wide.push_back(Wide{});
            EXPECT_EQ(reinterpret_cast<uintptr_t>(wide.data()) % kAlignment, 0u) << "after " << i + 1 << " elements";
        }
#endif
    }

    TEST(EngineAllocatorTest, AnArenaVectorStillCompilesNextToIt) {
        LinearArena arena{"EngineAllocatorTest"};
        ArenaVector<int> scratch{ArenaAllocator(arena)};
        Vector<int> heap;
        for (size_t i = 0; i < kInlineCapacity; ++i) {
            scratch.push_back(static_cast<int>(i));
            heap.push_back(static_cast<int>(i));
        }
        EXPECT_EQ(scratch.size(), heap.size());
    }

#if BE_PROFILER_ALLOC

    TEST(EngineAllocatorTest, TheBlocksOfAContainerAreCountedUnderTheTagInForce) {
        const AllocTagTotals before = AllocTracker::GetTagTotals(MemTag::Scene);
        {
            BE_MEM_TAG(Scene);
            Vector<int> values;
            values.reserve(kCount);
            EXPECT_GE(AllocTracker::GetTagTotals(MemTag::Scene).liveBytes, before.liveBytes + kCount * sizeof(int));
        }
        const AllocTagTotals after = AllocTracker::GetTagTotals(MemTag::Scene);
        EXPECT_GE(after.count, before.count + 1);
        EXPECT_EQ(after.liveBytes, before.liveBytes);
        EXPECT_GE(after.frees, before.frees + 1);
    }

#endif

#if BE_PROFILER_ALLOC && EASTL_NAME_ENABLED

    namespace {

        /// The allocations made under the allocation name @p name, summed over the zones (AllocTracker::GetNameStats)
        AllocSiteStats NameTotals(const char* name) {
            AllocSiteStats totals;
            for (const AllocSiteStats& row : AllocTracker::GetNameStats()) {
                if (row.name != nullptr && std::strcmp(row.name, name) == 0) {
                    totals.count += row.count;
                    totals.bytes += row.bytes;
                }
            }
            return totals;
        }

    }  // namespace

    TEST(EngineAllocatorTest, ABlockIsReportedUnderTheNameOfItsAllocator) {
        constexpr const char* kName = "EngineAllocatorTest.Named";
        constexpr size_t kBytes = 96;
        const AllocSiteStats before = NameTotals(kName);
        EngineAllocator allocator{kName};
        void* block = allocator.allocate(kBytes);
        allocator.deallocate(block, kBytes);
        const AllocSiteStats after = NameTotals(kName);
        EXPECT_EQ(after.count, before.count + 1);
        EXPECT_EQ(after.bytes, before.bytes + kBytes);
    }

    TEST(EngineAllocatorTest, TheContainerNameReachesTheTracker) {
        constexpr const char* kName = "EngineAllocatorTest.Vector";
        const AllocSiteStats before = NameTotals(kName);
        Vector<int> values{EngineAllocator{kName}};
        values.reserve(kCount);
        EXPECT_GE(NameTotals(kName).bytes, before.bytes + kCount * sizeof(int));
    }

#endif

}  // namespace BECore::Tests
