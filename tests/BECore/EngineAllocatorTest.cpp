#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Memory/EngineAllocator.h>
#include <BECore/Memory/MemTag.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <EASTL/fixed_string.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>
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

        template <typename T>
        using EngineVector = eastl::vector<T, EngineAllocator>;
        using EngineString = eastl::basic_string<char, EngineAllocator>;
        using EngineMap = eastl::hash_map<int, int, eastl::hash<int>, eastl::equal_to<int>, EngineAllocator>;
        using EngineTree = eastl::map<int, int, eastl::less<int>, EngineAllocator>;

    }  // namespace

    TEST(EngineAllocatorTest, ItIsExactlyAsLargeAsTheEastlAllocator) {
        EXPECT_EQ(sizeof(EngineAllocator), sizeof(eastl::allocator));
        EXPECT_EQ(alignof(EngineAllocator), alignof(eastl::allocator));
        EXPECT_EQ(sizeof(EngineString), sizeof(eastl::string));
        EXPECT_EQ(sizeof(EngineVector<int>), sizeof(eastl::vector<int>));
    }

    TEST(EngineAllocatorTest, AllInstancesAreEqual) {
        EXPECT_TRUE(EngineAllocator{} == EngineAllocator{"named"});
        EXPECT_FALSE(EngineAllocator{} != EngineAllocator{"named"});
    }

    TEST(EngineAllocatorTest, AVectorGrowsCopiesMovesAndSwaps) {
        EngineVector<int> values;
        for (int i = 0; i < kCount; ++i) {
            values.push_back(i);
        }
        ASSERT_EQ(values.size(), static_cast<size_t>(kCount));
        EXPECT_EQ(values[kCount - 1], kCount - 1);

        EngineVector<int> copy = values;
        EXPECT_EQ(copy, values);

        const int* storage = values.data();
        EngineVector<int> moved = std::move(values);
        EXPECT_EQ(moved.data(), storage);

        EngineVector<int> other{1, 2, 3};
        moved.swap(other);
        EXPECT_EQ(moved.size(), 3u);
        EXPECT_EQ(other.data(), storage);
    }

    TEST(EngineAllocatorTest, AStringLongerThanItsInlineBufferGrowsCopiesAndMoves) {
        EngineString text;
        text.append(kCount, 'x');
        EXPECT_EQ(text.size(), static_cast<size_t>(kCount));

        EngineString copy = text;
        EXPECT_EQ(copy, text);

        EngineString moved = std::move(text);
        EXPECT_EQ(moved.size(), static_cast<size_t>(kCount));
        EXPECT_EQ(moved[kCount - 1], 'x');

        EngineString other{"short"};
        moved.swap(other);
        EXPECT_EQ(moved, "short");
        EXPECT_EQ(other.size(), static_cast<size_t>(kCount));
    }

    TEST(EngineAllocatorTest, ANodeContainerGrowsCopiesMovesAndSwaps) {
        EngineMap map;
        EngineTree tree;
        for (int i = 0; i < kCount; ++i) {
            map[i] = i * 2;
            tree[i] = i * 3;
        }
        EXPECT_EQ(map.size(), static_cast<size_t>(kCount));
        EXPECT_EQ(map.at(kCount - 1), (kCount - 1) * 2);
        EXPECT_EQ(tree.at(kCount - 1), (kCount - 1) * 3);

        EngineMap mapCopy = map;
        EngineTree treeCopy = tree;
        EXPECT_EQ(mapCopy.size(), map.size());
        EXPECT_EQ(treeCopy, tree);

        EngineMap movedMap = std::move(map);
        EngineTree movedTree = std::move(tree);
        EngineMap emptyMap;
        movedMap.swap(emptyMap);
        EXPECT_TRUE(movedMap.empty());
        EXPECT_EQ(emptyMap.size(), static_cast<size_t>(kCount));
        EXPECT_EQ(movedTree.size(), static_cast<size_t>(kCount));
    }

    TEST(EngineAllocatorTest, AFixedVectorTakesItsOverflowFromTheAllocator) {
        eastl::fixed_vector<int, kInlineCapacity, true, EngineAllocator> values;
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
        eastl::fixed_string<char, kInlineStringCapacity, true, EngineAllocator> text;
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
        EngineVector<Wide> wide;
        for (size_t i = 0; i < kGrowthSteps; ++i) {
            wide.push_back(Wide{});
            EXPECT_EQ(reinterpret_cast<uintptr_t>(wide.data()) % kAlignment, 0u) << "after " << i + 1 << " elements";
        }
#endif
    }

    TEST(EngineAllocatorTest, AnArenaVectorStillCompilesNextToIt) {
        LinearArena arena{"EngineAllocatorTest"};
        ArenaVector<int> scratch{ArenaAllocator(arena)};
        EngineVector<int> heap;
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
            EngineVector<int> values;
            values.reserve(kCount);
            EXPECT_GE(AllocTracker::GetTagTotals(MemTag::Scene).liveBytes, before.liveBytes + kCount * sizeof(int));
        }
        const AllocTagTotals after = AllocTracker::GetTagTotals(MemTag::Scene);
        EXPECT_GE(after.count, before.count + 1);
        EXPECT_EQ(after.liveBytes, before.liveBytes);
        EXPECT_GE(after.frees, before.frees + 1);
    }

#endif

}  // namespace BECore::Tests
