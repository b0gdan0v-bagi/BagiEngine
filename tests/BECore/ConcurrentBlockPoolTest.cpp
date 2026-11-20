#include <BECore/Memory/ArenaPoison.h>
#include <BECore/Memory/ConcurrentBlockPool.h>
#include <Common/AllocCheck.h>
#include <Common/Wait.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

namespace BECore::Tests {

    namespace {

        constexpr size_t kBlockBytes = 64;
        constexpr size_t kBlocksPerPage = 20;
        // What one thread takes from an empty pool in a call of Allocate: the block and a batch for its list
        constexpr size_t kFirstTake = 1 + ConcurrentBlockPool::kCacheBatch;

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

#if BE_ARENA_ASAN
        bool IsPoisoned(const void* ptr) {
            return __asan_address_is_poisoned(ptr) != 0;
        }
#endif

        /// Every byte of a block is its owner's tag: another owner's write shows as a different byte
        void Fill(void* block, size_t size, uint8_t tag) {
            std::memset(block, tag, size);
        }

        bool Holds(const void* block, size_t size, uint8_t tag) {
            const auto* bytes = static_cast<const uint8_t*>(block);
            return std::all_of(bytes, bytes + size, [tag](uint8_t byte) { return byte == tag; });
        }

        /// A pool made at static initialisation: nothing about it needs the engine
        constinit ConcurrentBlockPool g_staticPool("StaticConcurrentPool", 24, 8);

    }  // namespace

    TEST(ConcurrentBlockPoolTest, AConstinitPoolIsUsable) {
        void* block = g_staticPool.Allocate();
        ASSERT_NE(block, nullptr);
        EXPECT_EQ(g_staticPool.GetLiveCount(), 1u);
        g_staticPool.Deallocate(block);
        EXPECT_EQ(g_staticPool.GetLiveCount(), 0u);
    }

    TEST(ConcurrentBlockPoolTest, AFreedBlockIsTheNextOneHandedOutOnTheSameThread) {
        ConcurrentBlockPool pool("ConcurrentReuse", kBlockBytes, 8);
        void* first = pool.Allocate();
        pool.Deallocate(first);
        EXPECT_EQ(pool.Allocate(), first);
        pool.Deallocate(first);
    }

    TEST(ConcurrentBlockPoolTest, SeveralThreadsGetNoBlockTwiceAndEndAtZeroLive) {
        constexpr int kThreads = 4;
        constexpr int kRounds = 20000;
        constexpr size_t kHeld = 48;
        ConcurrentBlockPool pool("ConcurrentThreads", kBlockBytes, 8);
        std::atomic<int> corrupted{0};
        Vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&, tag = static_cast<uint8_t>(t + 1)] {
                Vector<void*> held;
                for (int i = 0; i < kRounds; ++i) {
                    if (held.size() < kHeld) {
                        void* block = pool.Allocate();
                        Fill(block, kBlockBytes, tag);
                        held.push_back(block);
                    } else {
                        for (void* block : held) {
                            if (!Holds(block, kBlockBytes, tag)) {
                                corrupted.fetch_add(1);
                            }
                            pool.Deallocate(block);
                        }
                        held.clear();
                    }
                }
                for (void* block : held) {
                    if (!Holds(block, kBlockBytes, tag)) {
                        corrupted.fetch_add(1);
                    }
                    pool.Deallocate(block);
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        EXPECT_EQ(corrupted.load(), 0);
        EXPECT_EQ(pool.GetLiveCount(), 0u);
    }

    TEST(ConcurrentBlockPoolTest, ABlockFreedByAnotherThreadIsReusedAndThePagesStopGrowing) {
        constexpr size_t kTotal = 100000;
        constexpr size_t kInFlight = 256;
        constexpr size_t kPagesLimit = 8;  // blocks in flight and in the lists of two threads fit in a few pages; without the spill every block is a new one
        ConcurrentBlockPool pool("ConcurrentProducerConsumer", kBlockBytes, 8);
        std::mutex queueMutex;
        std::deque<void*> queue;
        std::atomic<size_t> outstanding{0};

        std::thread consumer([&] {
            for (size_t freed = 0; freed < kTotal;) {
                void* block = nullptr;
                {
                    std::lock_guard lock(queueMutex);
                    if (!queue.empty()) {
                        block = queue.front();
                        queue.pop_front();
                    }
                }
                if (block == nullptr) {
                    std::this_thread::yield();
                    continue;
                }
                pool.Deallocate(block);
                outstanding.fetch_sub(1);
                ++freed;
            }
        });
        for (size_t made = 0; made < kTotal; ++made) {
            while (outstanding.load() >= kInFlight) {
                std::this_thread::yield();
            }
            void* block = pool.Allocate();
            outstanding.fetch_add(1);
            std::lock_guard lock(queueMutex);
            queue.push_back(block);
        }
        consumer.join();

        EXPECT_EQ(pool.GetLiveCount(), 0u);
        EXPECT_LE(pool.GetPageCount(), kPagesLimit);
    }

    TEST(ConcurrentBlockPoolTest, AThreadsEndGivesItsFreeBlocksBack) {
        ConcurrentBlockPool pool("ConcurrentThreadEnd", kBlockBytes, 8, kBlocksPerPage);
        std::thread worker([&] {
            void* block = pool.Allocate();
            pool.Deallocate(block);
        });
        worker.join();
        EXPECT_EQ(pool.GetLiveCount(), 0u);
        ASSERT_EQ(pool.GetPageCount(), 1u);

        // The worker took one batch and its end spilled it; taking a batch here needs no new page
        Vector<void*> blocks;
        for (size_t i = 0; i < kFirstTake; ++i) {
            blocks.push_back(pool.Allocate());
        }
        EXPECT_EQ(pool.GetPageCount(), 1u);
        for (void* block : blocks) {
            pool.Deallocate(block);
        }
    }

    TEST(ConcurrentBlockPoolTest, DrainThreadCacheGivesBlocksBackWhileTheThreadLives) {
        ConcurrentBlockPool pool("ConcurrentDrain", kBlockBytes, 8, kBlocksPerPage);
        std::atomic<bool> drained{false};
        std::atomic<bool> finish{false};
        std::thread worker([&] {
            pool.Deallocate(pool.Allocate());
            ConcurrentBlockPool::DrainThreadCache();
            drained.store(true);
            WaitUntil([&] { return finish.load(); });
        });
        ASSERT_TRUE(WaitUntil([&] { return drained.load(); }));

        Vector<void*> blocks;
        for (size_t i = 0; i < kFirstTake; ++i) {
            blocks.push_back(pool.Allocate());
        }
        EXPECT_EQ(pool.GetPageCount(), 1u);
        for (void* block : blocks) {
            pool.Deallocate(block);
        }
        finish.store(true);
        worker.join();
    }

    TEST(ConcurrentBlockPoolTest, ThePoolMayDieWhileAThreadStillKeepsItsBlocks) {
        std::atomic<bool> cached{false};
        std::atomic<bool> poolGone{false};
        UniquePtr<std::thread> worker;
        {
            auto pool = MakeUnique<ConcurrentBlockPool>("ConcurrentDies", kBlockBytes, 8);
            worker = MakeUnique<std::thread>([&] {
                pool->Deallocate(pool->Allocate());
                cached.store(true);
                WaitUntil([&] { return poolGone.load(); });
                // The thread ends here with a list of a pool that no longer exists
            });
            ASSERT_TRUE(WaitUntil([&] { return cached.load(); }));
            EXPECT_EQ(pool->GetLiveCount(), 0u);
        }
        poolGone.store(true);
        worker->join();
    }

    TEST(ConcurrentBlockPoolTest, ANewPoolAtTheAddressOfADeadOneIsNotMistakenForIt) {
        alignas(ConcurrentBlockPool) std::byte storage[sizeof(ConcurrentBlockPool)];
        auto* first = new (storage) ConcurrentBlockPool("ConcurrentSameAddress", kBlockBytes, 8, kBlocksPerPage);
        std::atomic<int> stage{0};
        std::atomic<bool> intact{false};
        std::thread worker([&] {
            first->Deallocate(first->Allocate());
            stage.store(1);
            WaitUntil([&] { return stage.load() == 2; });
            // The same address, another pool: the list kept for the dead one must not be handed out
            auto* second = reinterpret_cast<ConcurrentBlockPool*>(storage);
            void* block = second->Allocate();
            Fill(block, kBlockBytes, 0x5A);
            intact.store(Holds(block, kBlockBytes, 0x5A) && second->GetLiveCount() == 1u);
            second->Deallocate(block);
            stage.store(3);
            WaitUntil([&] { return stage.load() == 4; });
        });
        ASSERT_TRUE(WaitUntil([&] { return stage.load() == 1; }));
        std::destroy_at(first);
        auto* second = new (storage) ConcurrentBlockPool("ConcurrentSameAddress", kBlockBytes, 8, kBlocksPerPage);
        stage.store(2);
        ASSERT_TRUE(WaitUntil([&] { return stage.load() == 3; }));
        EXPECT_TRUE(intact.load());
        EXPECT_EQ(second->GetLiveCount(), 0u);
        EXPECT_EQ(second->GetPageCount(), 1u);
        // The worker ends with a list of the second pool, which is gone by then too
        std::destroy_at(second);
        stage.store(4);
        worker.join();
    }

    TEST(ConcurrentBlockPoolTest, AThreadUsingMorePoolsThanItHasListsSpillsTheOldestWithoutLosingBlocks) {
        constexpr size_t kPools = ConcurrentBlockPool::kCachedPools + 3;
        Vector<UniquePtr<ConcurrentBlockPool>> pools;
        for (size_t i = 0; i < kPools; ++i) {
            pools.push_back(MakeUnique<ConcurrentBlockPool>("ConcurrentEvict", kBlockBytes, 8, kBlocksPerPage));
        }
        for (int round = 0; round < 3; ++round) {
            for (auto& pool : pools) {
                pool->Deallocate(pool->Allocate());
            }
        }
        for (auto& pool : pools) {
            EXPECT_EQ(pool->GetLiveCount(), 0u);
            EXPECT_EQ(pool->GetPageCount(), 1u) << "a spilled list is reused, so no pool needs a second page for one batch";
        }
        ConcurrentBlockPool::DrainThreadCache();
        for (auto& pool : pools) {
            Vector<void*> blocks;
            for (size_t i = 0; i < kFirstTake; ++i) {
                blocks.push_back(pool->Allocate());
            }
            EXPECT_EQ(pool->GetPageCount(), 1u);
            for (void* block : blocks) {
                pool->Deallocate(block);
            }
        }
    }

    TEST(ConcurrentBlockPoolTest, TheCountersFollowEveryOperation) {
        ConcurrentBlockPool pool("ConcurrentCounters", kBlockBytes, 8, kBlocksPerPage);
        constexpr size_t kBlocks = 50;
        Vector<void*> blocks;
        for (size_t i = 0; i < kBlocks; ++i) {
            blocks.push_back(pool.Allocate());
        }
        EXPECT_EQ(pool.GetLiveCount(), kBlocks);
        ConcurrentBlockPool::DrainThreadCache();
        Found found = Find("ConcurrentCounters");
        ASSERT_TRUE(found.found);
        EXPECT_EQ(found.entry.live, kBlocks);
        EXPECT_EQ(found.entry.allocations, kBlocks);
        EXPECT_EQ(found.entry.blockSize, pool.GetBlockSize());
        EXPECT_EQ(found.entry.pages, pool.GetPageCount());
        EXPECT_GE(found.entry.pages, 3u);
        EXPECT_GT(found.entry.pageBytes, 0u);

        for (void* block : blocks) {
            pool.Deallocate(block);
        }
        EXPECT_EQ(pool.GetLiveCount(), 0u);
        ConcurrentBlockPool::DrainThreadCache();
        found = Find("ConcurrentCounters");
        EXPECT_EQ(found.entry.live, 0u);
        EXPECT_EQ(found.entry.peak, kBlocks);
        EXPECT_EQ(found.entry.allocations, kBlocks);
        EXPECT_EQ(pool.GetLiveCount(), 0u);
    }

    TEST(ConcurrentBlockPoolTest, TheRegistryLagsAtMostAPublishIntervalAndIsExactAfterADrain) {
        ConcurrentBlockPool pool("ConcurrentPublish", kBlockBytes, 8);
        void* block = pool.Allocate();
        for (uint32_t i = 0; i < ConcurrentBlockPool::kPublishEvery; ++i) {
            pool.Deallocate(block);
            block = pool.Allocate();
        }
        // Hits of the list publish on their own: the registry has seen most of the calls without a drain
        const Found seen = Find("ConcurrentPublish");
        ASSERT_TRUE(seen.found);
        EXPECT_GT(seen.entry.allocations, 1u);
        pool.Deallocate(block);
        ConcurrentBlockPool::DrainThreadCache();
        const Found exact = Find("ConcurrentPublish");
        EXPECT_EQ(exact.entry.live, 0u);
        EXPECT_EQ(exact.entry.allocations, ConcurrentBlockPool::kPublishEvery + 1u);
    }

    TEST(ConcurrentBlockPoolTest, ACountedPoolLeavesItsSlotWhenItDies) {
        const uint32_t before = PoolStats::GetLiveCount();
        {
            ConcurrentBlockPool pool("ConcurrentSlot", kBlockBytes, 8);
            pool.Deallocate(pool.Allocate());
            EXPECT_EQ(PoolStats::GetLiveCount(), before + 1);
        }
        EXPECT_EQ(PoolStats::GetLiveCount(), before);
        EXPECT_FALSE(Find("ConcurrentSlot").found);
    }

    TEST(ConcurrentBlockPoolTest, ASteadyAllocateAndDeallocateMakesNoHeapAllocation) {
        ConcurrentBlockPool pool("ConcurrentSteady", kBlockBytes, 8);
        pool.Deallocate(pool.Allocate());
        ExpectNoAllocations([&] {
            for (int i = 0; i < 1000; ++i) {
                pool.Deallocate(pool.Allocate());
            }
        });
    }

#if ENGINE_ENABLE_ASSERTS
    TEST(ConcurrentBlockPoolTest, ABlockGivenBackTwiceAssertsAndIsNotCountedTwice) {
        ConcurrentBlockPool pool("ConcurrentDoubleFree", kBlockBytes, 8, kBlocksPerPage);
        void* block = pool.Allocate();
        pool.Deallocate(block);
        EXPECT_NONFATAL_FAILURE(pool.Deallocate(block), "twice");
        EXPECT_EQ(pool.GetLiveCount(), 0u);
    }

    TEST(ConcurrentBlockPoolTest, APointerOfNoBlockAsserts) {
        ConcurrentBlockPool pool("ConcurrentForeign", kBlockBytes, 8, kBlocksPerPage);
        auto* block = static_cast<std::byte*>(pool.Allocate());
        alignas(8) std::byte local[kBlockBytes];
        EXPECT_NONFATAL_FAILURE(pool.Deallocate(local), "no block");
        EXPECT_NONFATAL_FAILURE(pool.Deallocate(block + 1), "no block");
        EXPECT_EQ(pool.GetLiveCount(), 1u);
        pool.Deallocate(block);
    }

#if !BE_ARENA_ASAN
    TEST(ConcurrentBlockPoolTest, AWriteIntoABlockOfAThreadsListAssertsAtTheNextAllocate) {
        ConcurrentBlockPool pool("ConcurrentWriteAfterFree", kBlockBytes, 8, kBlocksPerPage);
        auto* block = static_cast<std::byte*>(pool.Allocate());
        pool.Deallocate(block);
        block[pool.GetBlockSize() - 1] = std::byte{0x01};
        void* again = nullptr;
        EXPECT_NONFATAL_FAILURE(again = pool.Allocate(), "written after");
        EXPECT_EQ(again, block);
        pool.Deallocate(again);
    }
#endif
#endif

#if BE_ARENA_ASAN
    TEST(ConcurrentBlockPoolTest, AFreedBlockIsPoisonedAndTheNextAllocateUnpoisonsIt) {
        ConcurrentBlockPool pool("ConcurrentPoison", 40, 8, kBlocksPerPage);
        auto* block = static_cast<std::byte*>(pool.Allocate());
        const size_t size = pool.GetBlockSize();
        EXPECT_FALSE(IsPoisoned(block + size - 1));
        pool.Deallocate(block);
        EXPECT_TRUE(IsPoisoned(block));
        EXPECT_TRUE(IsPoisoned(block + size - 1));
        EXPECT_EQ(pool.Allocate(), block);
        EXPECT_FALSE(IsPoisoned(block));
        EXPECT_FALSE(IsPoisoned(block + size - 1));
        pool.Deallocate(block);
    }

    TEST(ConcurrentBlockPoolTest, BlocksOfAThreadsListAreNotHandedOutPoisoned) {
        ConcurrentBlockPool pool("ConcurrentPoisonList", 40, 8, kBlocksPerPage);
        Vector<void*> blocks;
        for (size_t i = 0; i < 5; ++i) {
            void* block = pool.Allocate();
            EXPECT_FALSE(IsPoisoned(block));
            blocks.push_back(block);
        }
        for (void* block : blocks) {
            pool.Deallocate(block);
        }
    }
#endif

    namespace {

        using Clock = std::chrono::steady_clock;

        constexpr size_t kCostBatch = 8;
        constexpr size_t kCostRounds = 500000;

        template <typename TAllocate, typename TFree>
        double NanosPerPair(int threads, TAllocate&& allocate, TFree&& release) {
            std::atomic<int> ready{0};
            std::atomic<bool> go{false};
            Vector<std::thread> workers;
            std::atomic<int64_t> totalNanos{0};
            for (int t = 0; t < threads; ++t) {
                workers.emplace_back([&] {
                    ready.fetch_add(1);
                    while (!go.load()) {
                        std::this_thread::yield();
                    }
                    Vector<void*> held(kCostBatch);
                    const auto start = Clock::now();
                    for (size_t round = 0; round < kCostRounds; ++round) {
                        for (void*& block : held) {
                            block = allocate();
                        }
                        for (void* block : held) {
                            release(block);
                        }
                    }
                    totalNanos.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
                });
            }
            while (ready.load() < threads) {
                std::this_thread::yield();
            }
            go.store(true);
            for (std::thread& worker : workers) {
                worker.join();
            }
            return static_cast<double>(totalNanos.load()) / static_cast<double>(threads * kCostRounds * kCostBatch);
        }

    }  // namespace

    /// A benchmark, not a check: the set of .claude/rules/perf-project.md runs it in macos-release (Memory/CLAUDE.md, "Pools")
    TEST(ConcurrentBlockPoolCostTest, DISABLED_PooledAgainstNewAndDelete) {
        for (const int threads : {1, 4}) {
            ConcurrentBlockPool pool("ConcurrentCost", kBlockBytes, 8);
            const double pooled = NanosPerPair(threads, [&] { return pool.Allocate(); }, [&](void* block) { pool.Deallocate(block); });
            const double heap = NanosPerPair(threads, [] { return ::operator new(kBlockBytes); }, [](void* block) { ::operator delete(block); });
            std::cout << "ConcurrentBlockPoolCost threads=" << threads << " pooled_ns=" << pooled << " heap_ns=" << heap << "\n";
        }
    }

}  // namespace BECore::Tests
