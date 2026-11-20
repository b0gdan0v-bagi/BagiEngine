#include <BECore/Memory/ArenaPoison.h>
#include <BECore/Memory/BlockPool.h>
#include <Common/AllocCheck.h>
#include <algorithm>
#include <cstring>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr size_t kSmallPage = 4;

        /// The entry called @p name; `found` is false when the registry has none
        struct Found {
            bool found = false;
            PoolStatsEntry entry;
        };

        Found Find(const char* name) {
            for (const PoolStatsEntry& entry : GetPoolStats()) {
                if (std::strcmp(entry.name, name) == 0) {
                    return {true, entry};
                }
            }
            return {};
        }

        bool IsAligned(const void* ptr, size_t alignment) {
            return reinterpret_cast<uintptr_t>(ptr) % alignment == 0;
        }

#if BE_ARENA_ASAN
        bool IsPoisoned(const void* ptr) {
            return __asan_address_is_poisoned(ptr) != 0;
        }
#endif

        struct alignas(128) OverAligned {
            char bytes[200];
        };

        /// A pool made at static initialisation, and a block taken before main: nothing about it needs the engine
        constinit BlockPool g_staticPool("StaticBlockPool", 24, 8, kSmallPage);
        void* const g_earlyBlock = g_staticPool.Allocate();

    }  // namespace

    TEST(BlockPoolTest, AFreedBlockIsTheNextOneHandedOut) {
        BlockPool pool("ReusePool", 32, 8, kSmallPage);
        void* first = pool.Allocate();
        void* second = pool.Allocate();
        pool.Deallocate(first);
        EXPECT_EQ(pool.Allocate(), first);
        pool.Deallocate(second);
        pool.Deallocate(first);
        EXPECT_EQ(pool.Allocate(), first);
        EXPECT_EQ(pool.Allocate(), second);
        pool.Deallocate(first);
        pool.Deallocate(second);
    }

    TEST(BlockPoolTest, EveryAlignmentFromOneToSixtyFourHoldsOnEveryBlock) {
        for (size_t alignment = 1; alignment <= 64; alignment *= 2) {
            BlockPool pool("AlignedPool", 13, alignment, kSmallPage);
            EXPECT_EQ(pool.GetBlockSize() % alignment, 0u) << alignment;
            EXPECT_GE(pool.GetBlockSize(), 13u) << alignment;
            Vector<void*> blocks;
            for (int i = 0; i < 3 * static_cast<int>(kSmallPage); ++i) {
                void* block = pool.Allocate();
                EXPECT_TRUE(IsAligned(block, alignment)) << alignment << " block " << i;
                std::memset(block, 0x5A, 13);
                blocks.push_back(block);
            }
            for (void* block : blocks) {
                pool.Deallocate(block);
            }
        }
    }

    TEST(BlockPoolTest, ABlockOfAnOverAlignedTypeIsAlignedForIt) {
        BlockPool pool("OverAlignedPool", sizeof(OverAligned), alignof(OverAligned), kSmallPage);
        Vector<OverAligned*> blocks;
        for (int i = 0; i < 2 * static_cast<int>(kSmallPage); ++i) {
            auto* block = static_cast<OverAligned*>(pool.Allocate());
            EXPECT_TRUE(IsAligned(block, alignof(OverAligned))) << i;
            std::memset(block, 0x11, sizeof(OverAligned));
            blocks.push_back(block);
        }
        for (OverAligned* block : blocks) {
            pool.Deallocate(block);
        }
    }

    TEST(BlockPoolTest, FreeingEverythingKeepsThePages) {
        BlockPool pool("KeptPages", 40, 8, kSmallPage);
        constexpr size_t kBlocks = 2 * kSmallPage + 1;
        Vector<void*> blocks;
        for (size_t i = 0; i < kBlocks; ++i) {
            blocks.push_back(pool.Allocate());
        }
        EXPECT_EQ(pool.GetLiveCount(), kBlocks);
        EXPECT_EQ(pool.GetPageCount(), 3u);
        for (void* block : blocks) {
            pool.Deallocate(block);
        }
        EXPECT_EQ(pool.GetLiveCount(), 0u);
        EXPECT_EQ(pool.GetPageCount(), 3u);

        blocks.clear();
        for (size_t i = 0; i < kBlocks; ++i) {
            blocks.push_back(pool.Allocate());
        }
        EXPECT_EQ(pool.GetPageCount(), 3u);
        for (void* block : blocks) {
            pool.Deallocate(block);
        }
    }

    TEST(BlockPoolTest, BlocksOfOnePageDoNotOverlapAndStayOnTheStride) {
        BlockPool pool("StridePool", 20, 8, kSmallPage);
        Vector<std::byte*> blocks;
        for (size_t i = 0; i < kSmallPage; ++i) {
            blocks.push_back(static_cast<std::byte*>(pool.Allocate()));
        }
        EXPECT_EQ(pool.GetPageCount(), 1u);
        for (size_t i = 0; i < blocks.size(); ++i) {
            std::memset(blocks[i], static_cast<int>(i + 1), pool.GetBlockSize());
        }
        for (size_t i = 0; i < blocks.size(); ++i) {
            for (size_t byte = 0; byte < pool.GetBlockSize(); ++byte) {
                ASSERT_EQ(blocks[i][byte], std::byte(i + 1)) << "block " << i << " byte " << byte;
            }
        }
        std::sort(blocks.begin(), blocks.end());
        for (size_t i = 1; i < blocks.size(); ++i) {
            EXPECT_EQ(blocks[i] - blocks[i - 1], static_cast<std::ptrdiff_t>(pool.GetBlockSize()));
        }
        for (std::byte* block : blocks) {
            pool.Deallocate(block);
        }
    }

    TEST(BlockPoolTest, TheCountersFollowEveryOperation) {
        BlockPool pool("CountedPool", 48, 16, kSmallPage);
        EXPECT_FALSE(Find("CountedPool").found) << "nothing is registered before the first Allocate";

        Vector<void*> blocks;
        for (size_t i = 0; i < kSmallPage + 1; ++i) {
            blocks.push_back(pool.Allocate());
        }
        pool.Deallocate(blocks.back());
        blocks.pop_back();
        pool.Deallocate(blocks.back());
        blocks.pop_back();

        const Found found = Find("CountedPool");
        ASSERT_TRUE(found.found);
        EXPECT_EQ(found.entry.blockSize, pool.GetBlockSize());
        EXPECT_EQ(found.entry.live, kSmallPage - 1);
        EXPECT_EQ(found.entry.peak, kSmallPage + 1);
        EXPECT_EQ(found.entry.pages, 2u);
        EXPECT_EQ(found.entry.allocations, kSmallPage + 1);
        EXPECT_GT(found.entry.pageBytes, 2 * kSmallPage * pool.GetBlockSize() - 1);
        for (void* block : blocks) {
            pool.Deallocate(block);
        }
    }

    TEST(BlockPoolTest, ADeadPoolLeavesNoEntry) {
        const uint32_t before = PoolStats::GetLiveCount();
        {
            BlockPool pool("DyingPool", 16, 8);
            pool.Deallocate(pool.Allocate());
            EXPECT_EQ(PoolStats::GetLiveCount(), before + 1);
        }
        EXPECT_EQ(PoolStats::GetLiveCount(), before);
    }

    TEST(BlockPoolTest, AUncountedPoolRegistersNothing) {
        const uint32_t before = PoolStats::GetLiveCount();
        BlockPool pool("UncountedPool", 16, 8, kSmallPage, false);
        pool.Deallocate(pool.Allocate());
        EXPECT_EQ(PoolStats::GetLiveCount(), before);
    }

    TEST(BlockPoolTest, AConstinitPoolWorksFromAStaticInitialiser) {
        ASSERT_NE(g_earlyBlock, nullptr);
        EXPECT_EQ(g_staticPool.GetLiveCount(), 1u);
        std::memset(g_earlyBlock, 0x77, 24);
        g_staticPool.Deallocate(g_earlyBlock);
        EXPECT_EQ(g_staticPool.GetLiveCount(), 0u);
    }

    TEST(BlockPoolTest, ASteadyAllocateAndDeallocateMakesNoHeapAllocation) {
        BlockPool pool("SteadyPool", 64, 8);
        pool.Deallocate(pool.Allocate());  // the first page
        ExpectNoAllocations([&] {
            for (int i = 0; i < 1000; ++i) {
                void* block = pool.Allocate();
                pool.Deallocate(block);
            }
        });
    }

#if ENGINE_ENABLE_ASSERTS
    TEST(BlockPoolTest, ABlockGivenBackTwiceAssertsAndIsNotListedTwice) {
        BlockPool pool("DoubleFreePool", 32, 8, kSmallPage);
        void* block = pool.Allocate();
        pool.Deallocate(block);
        EXPECT_NONFATAL_FAILURE(pool.Deallocate(block), "twice");
        void* first = pool.Allocate();
        void* second = pool.Allocate();
        EXPECT_EQ(first, block);
        EXPECT_NE(second, block);
        pool.Deallocate(first);
        pool.Deallocate(second);
    }

    TEST(BlockPoolTest, APointerOfNoBlockAssertsAndChangesNothing) {
        BlockPool pool("ForeignPool", 32, 8, kSmallPage);
        auto* block = static_cast<std::byte*>(pool.Allocate());
        alignas(8) std::byte local[64];
        EXPECT_NONFATAL_FAILURE(pool.Deallocate(local), "no block");
        EXPECT_NONFATAL_FAILURE(pool.Deallocate(block + 1), "no block");
        // The block after the bump pointer was never handed out
        EXPECT_NONFATAL_FAILURE(pool.Deallocate(block + pool.GetBlockSize()), "no block");
        EXPECT_EQ(pool.GetLiveCount(), 1u);
        pool.Deallocate(block);
    }

#if !BE_ARENA_ASAN
    TEST(BlockPoolTest, AWriteIntoAFreedBlockAssertsAtTheNextAllocate) {
        BlockPool pool("WriteAfterFree", 64, 8, kSmallPage);
        auto* block = static_cast<std::byte*>(pool.Allocate());
        pool.Deallocate(block);
        block[pool.GetBlockSize() - 1] = std::byte{0x01};
        void* again = nullptr;
        EXPECT_NONFATAL_FAILURE(again = pool.Allocate(), "written after");
        EXPECT_EQ(again, block);
        pool.Deallocate(again);
    }

    TEST(BlockPoolTest, AFreedBlockWithItsMarkOverwrittenIsReportedAndTheListIsNotFollowed) {
        BlockPool pool("MarkOverwritten", 64, 8, kSmallPage);
        auto* block = static_cast<std::byte*>(pool.Allocate());
        void* other = pool.Allocate();
        pool.Deallocate(other);
        pool.Deallocate(block);
        std::memset(block, 0xEE, 2 * sizeof(void*));
        void* again = nullptr;
        EXPECT_NONFATAL_FAILURE(again = pool.Allocate(), "mark");
        EXPECT_EQ(again, block);
        // The overwritten link was not trusted, so the next block comes from the bump, not from a garbage address
        void* next = pool.Allocate();
        EXPECT_NE(next, nullptr);
        EXPECT_NE(next, other);
        pool.Deallocate(next);
        pool.Deallocate(again);
    }

    TEST(BlockPoolTest, ADebugFreedBlockHoldsThePatternBesideItsLink) {
        BlockPool pool("PatternPool", 64, 8, kSmallPage);
        auto* block = static_cast<std::byte*>(pool.Allocate());
        std::memset(block, 0x01, pool.GetBlockSize());
        pool.Deallocate(block);
        for (size_t i = 2 * sizeof(void*); i < pool.GetBlockSize(); ++i) {
            EXPECT_EQ(block[i], std::byte{ArenaPoison::kFreedPattern}) << i;
        }
    }
#endif
#endif

#if BE_ARENA_ASAN
    TEST(BlockPoolTest, AFreedBlockIsPoisonedAndTheNextAllocateUnpoisonsExactlyItsBytes) {
        BlockPool pool("PoisonedPool", 40, 8, kSmallPage);
        auto* block = static_cast<std::byte*>(pool.Allocate());
        const size_t size = pool.GetBlockSize();
        EXPECT_FALSE(IsPoisoned(block));
        EXPECT_FALSE(IsPoisoned(block + size - 1));
        EXPECT_TRUE(IsPoisoned(block + size)) << "a block of the page nobody asked for";

        pool.Deallocate(block);
        EXPECT_TRUE(IsPoisoned(block));
        EXPECT_TRUE(IsPoisoned(block + size / 2));
        EXPECT_TRUE(IsPoisoned(block + size - 1));

        EXPECT_EQ(pool.Allocate(), block);
        EXPECT_FALSE(IsPoisoned(block));
        EXPECT_FALSE(IsPoisoned(block + size - 1));
        EXPECT_TRUE(IsPoisoned(block + size));
        pool.Deallocate(block);
    }

    TEST(BlockPoolTest, AFreedBlockStaysPoisonedWhileTheListReadsItsLink) {
        BlockPool pool("LinkPoisonPool", 40, 8, kSmallPage);
        void* first = pool.Allocate();
        void* second = pool.Allocate();
        pool.Deallocate(first);
        pool.Deallocate(second);
        // Both are on the list: allocating the head reads the link of a poisoned block, and the other stays poisoned
        EXPECT_EQ(pool.Allocate(), second);
        EXPECT_TRUE(IsPoisoned(first));
        EXPECT_EQ(pool.Allocate(), first);
        pool.Deallocate(first);
        pool.Deallocate(second);
    }
#endif

}  // namespace BECore::Tests
