#include <BECore/Memory/ArenaPoison.h>
#include <BECore/Memory/ScratchArena.h>
#include <cstring>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        constexpr size_t kBlock = 128;

        uint32_t CountScratchEntries() {
            uint32_t count = 0;
            for (const ArenaStatsEntry& entry : GetArenaStats()) {
                if (std::strcmp(entry.name, "Scratch") == 0) {
                    ++count;
                }
            }
            return count;
        }

    }  // namespace

    TEST(ScratchArenaTest, NestedScopesGiveBackInReverseAndTheInnerAddressIsReused) {
        ScratchScope outer;
        void* outerBlock = outer.Allocate(kBlock);
        const size_t usedOuter = ScratchArena::Get().GetBytesUsed();

        void* firstInner = nullptr;
        {
            ScratchScope inner;
            firstInner = inner.Allocate(kBlock);
            EXPECT_NE(firstInner, outerBlock);
            EXPECT_GT(ScratchArena::Get().GetBytesUsed(), usedOuter);
        }
        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), usedOuter);

        ScratchScope again;
        EXPECT_EQ(again.Allocate(kBlock), firstInner);
    }

    TEST(ScratchArenaTest, AScopeThatAllocatesNothingChangesNothing) {
        ScratchScope outer;
        EXPECT_NE(outer.Allocate(kBlock), nullptr);
        const size_t used = ScratchArena::Get().GetBytesUsed();
        {
            ScratchScope empty;
        }
        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), used);
    }

    TEST(ScratchArenaTest, MemoryComesBackAfterTheScopeAndThePeakStays) {
        // A size no other user of the scratch arenas reaches, so a peak this large is this thread's
        constexpr size_t kBig = 3 * ScratchArena::kChunkSize;
        bool peakKept = false;
        std::thread thread([&] {
            LinearArena& arena = ScratchArena::Get();
            {
                ScratchScope scope;
                EXPECT_NE(scope.Allocate(kBig), nullptr);
                EXPECT_GE(arena.GetBytesUsed(), kBig);
            }
            EXPECT_EQ(arena.GetBytesUsed(), 0u);
            for (const ArenaStatsEntry& entry : GetArenaStats()) {
                if (std::strcmp(entry.name, "Scratch") == 0 && entry.used == 0 && entry.peak >= kBig) {
                    peakKept = true;
                }
            }
        });
        thread.join();
        EXPECT_TRUE(peakKept);
    }

    TEST(ScratchArenaTest, GetIsUsableOnAThreadThatNeverTouchedTheEngine) {
        std::thread thread([] {
            ScratchScope scope;
            auto* data = scope.Allocate<int>(16);
            ASSERT_NE(data, nullptr);
            for (int i = 0; i < 16; ++i) {
                data[i] = i;
            }
            EXPECT_EQ(data[15], 15);
        });
        thread.join();
    }

    TEST(ScratchArenaTest, TwoThreadsGetDifferentArenasWithOwnCounters) {
        LinearArena* arenaOfMain = &ScratchArena::Get();
        LinearArena* arenaOfThread = nullptr;
        void* blockOfThread = nullptr;
        size_t usedOfThread = 0;
        ScratchScope scope;
        void* blockOfMain = scope.Allocate(kBlock);
        const size_t usedOfMain = arenaOfMain->GetBytesUsed();

        std::thread thread([&] {
            arenaOfThread = &ScratchArena::Get();
            ScratchScope threadScope;
            blockOfThread = threadScope.Allocate(kBlock);
            usedOfThread = arenaOfThread->GetBytesUsed();
        });
        thread.join();

        EXPECT_NE(arenaOfThread, arenaOfMain);
        EXPECT_NE(blockOfThread, blockOfMain);
        EXPECT_EQ(usedOfThread, kBlock);
        EXPECT_EQ(arenaOfMain->GetBytesUsed(), usedOfMain);
    }

    TEST(ScratchArenaTest, AThreadThatEndsLeavesNoStatsEntry) {
        const uint32_t before = CountScratchEntries();
        uint32_t during = 0;
        std::thread thread([&] {
            ScratchScope scope;
            EXPECT_NE(scope.Allocate(kBlock), nullptr);
            during = CountScratchEntries();
        });
        thread.join();
        EXPECT_EQ(CountScratchEntries(), before);
        EXPECT_GT(during, 0u);
    }

}  // namespace BECore::Tests
