#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kBufferSize = 16 * 1024;

        struct alignas(64) Wide {
            char bytes[64];
        };

        bool Inside(const void* ptr, const std::byte* buffer, size_t size) {
            const auto* bytes = static_cast<const std::byte*>(ptr);
            return bytes >= buffer && bytes < buffer + size;
        }

        /// An arena over a stack buffer: while the data fits, nothing comes from the heap
        struct BufferedArena {
            alignas(64) std::byte buffer[kBufferSize];
            LinearArena arena{"AllocatorTest", buffer, kBufferSize};
        };

    }  // namespace

    TEST(ArenaAllocatorTest, AVectorGrowsInsideTheArena) {
        BufferedArena memory;
        ArenaVector<int> values{ArenaAllocator(memory.arena)};
        for (int i = 0; i < 200; ++i) {
            values.push_back(i);
        }
        EXPECT_TRUE(Inside(values.data(), memory.buffer, kBufferSize));
        EXPECT_EQ(values[199], 199);
        EXPECT_GE(memory.arena.GetBytesUsed(), 200 * sizeof(int));
        EXPECT_EQ(memory.arena.GetChunkCount(), 1u);
    }

    TEST(ArenaAllocatorTest, AStringLongerThanItsInlineBufferLivesInTheArena) {
        BufferedArena memory;
        ArenaString text{ArenaAllocator(memory.arena)};
        text.append(200, 'x');
        EXPECT_TRUE(Inside(text.data(), memory.buffer, kBufferSize));
        EXPECT_EQ(text.size(), 200u);
        EXPECT_EQ(text[199], 'x');
    }

    TEST(ArenaAllocatorTest, AHashMapTakesItsNodesFromTheArena) {
        BufferedArena memory;
        eastl::hash_map<int, int, eastl::hash<int>, eastl::equal_to<int>, ArenaAllocator> map{ArenaAllocator(memory.arena)};  //-NoStd: the HashMap alias has no allocator
        for (int i = 0; i < 50; ++i) {
            map[i] = i * 2;
        }
        EXPECT_EQ(map.at(49), 98);
        EXPECT_GE(memory.arena.GetBytesUsed(), 50 * sizeof(int));
        for (const auto& entry : map) {
            EXPECT_TRUE(Inside(&entry, memory.buffer, kBufferSize));
        }
    }

    TEST(ArenaAllocatorTest, AnOverAlignedElementGetsItsAlignment) {
        BufferedArena memory;
        ArenaVector<Wide> values{ArenaAllocator(memory.arena)};
        (void)memory.arena.Allocate(3, 1);
        values.resize(4);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(values.data()) % alignof(Wide), 0u);
    }

    TEST(ArenaAllocatorTest, TwoVectorsOnOneArenaDoNotOverlap) {
        BufferedArena memory;
        ArenaVector<int> first{ArenaAllocator(memory.arena)};
        ArenaVector<int> second{ArenaAllocator(memory.arena)};
        for (int i = 0; i < 100; ++i) {
            first.push_back(i);
            second.push_back(-i);
        }
        for (int i = 0; i < 100; ++i) {
            ASSERT_EQ(first[i], i);
            ASSERT_EQ(second[i], -i);
        }
        const auto* firstBegin = reinterpret_cast<const std::byte*>(first.data());
        const auto* secondBegin = reinterpret_cast<const std::byte*>(second.data());
        const bool apart = firstBegin + first.capacity() * sizeof(int) <= secondBegin || secondBegin + second.capacity() * sizeof(int) <= firstBegin;
        EXPECT_TRUE(apart);
    }

    TEST(ArenaAllocatorTest, ResetGivesTheMemoryBack) {
        BufferedArena memory;
        {
            ArenaVector<int> values{ArenaAllocator(memory.arena)};
            values.resize(100);
        }
        const size_t used = memory.arena.GetBytesUsed();
        EXPECT_GT(used, 0u);
        memory.arena.Reset();
        EXPECT_EQ(memory.arena.GetBytesUsed(), 0u);

        ArenaVector<int> again{ArenaAllocator(memory.arena)};
        again.resize(100);
        EXPECT_EQ(memory.arena.GetBytesUsed(), used);
    }

    TEST(ArenaAllocatorTest, ADefaultConstructedContainerUsesTheScratchArenaAndTheScopeGivesItBack) {
        const size_t before = ScratchArena::Get().GetBytesUsed();
        {
            ScratchScope scope;
            ArenaVector<int> values;
            ArenaString text;
            EXPECT_EQ(&values.get_allocator().GetArena(), &ScratchArena::Get());
            EXPECT_EQ(&text.get_allocator().GetArena(), &ScratchArena::Get());
            values.resize(500);
            EXPECT_GE(ScratchArena::Get().GetBytesUsed(), before + 500 * sizeof(int));
        }
        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), before);
    }

    TEST(ArenaAllocatorTest, AllocatorsAreEqualWhenTheyShareAnArena) {
        BufferedArena one;
        BufferedArena other;
        EXPECT_EQ(ArenaAllocator(one.arena), ArenaAllocator(one.arena));
        EXPECT_NE(ArenaAllocator(one.arena), ArenaAllocator(other.arena));
        EXPECT_EQ(ArenaAllocator(), ArenaAllocator(ScratchArena::Get()));
    }

    TEST(ArenaAllocatorTest, ReservedDataThatFitsNeverTouchesTheHeap) {
        BufferedArena memory;
        // Everything the test itself needs from the heap is made before the counters are read
        (void)memory.arena.Allocate(1);
        const AllocTracker::ThreadCounters before = AllocTracker::GetThreadCounters();
        {
            ArenaVector<int> values{ArenaAllocator(memory.arena)};
            values.reserve(512);
            for (int i = 0; i < 512; ++i) {
                values.push_back(i);
            }
            ArenaString text{ArenaAllocator(memory.arena)};
            text.append(300, 'y');
        }
        const AllocTracker::ThreadCounters after = AllocTracker::GetThreadCounters();
        // The counters stay zero without ENGINE_PROFILER_ALLOC, so the check only bites in the profile preset
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(after.count, before.count);
        }
    }

}  // namespace BECore::Tests
