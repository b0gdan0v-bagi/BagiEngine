#include <Common/EngineEnvironment.h>
#include <Common/Wait.h>
#include <TaskSystem/ParallelFor.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/ThreadPool.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

namespace BECore::Tests {

    namespace {

        constexpr size_t kMinChunk = 64;
        constexpr size_t kBigCount = 100000;
        constexpr size_t kPrimeCount = 1009;
        constexpr size_t kInlinePool = 0;

        /// Worker counts the contract is held to: 0 is the inline pool (the single-thread build), the others are pools of that many workers
        constexpr size_t kWorkerCounts[] = {kInlinePool, 1, 2, 4};

        /// Runs @p test with a pool of @p workers (kInlinePool: the inline pool). A pool of its own per call: tests run beside others
        template <typename TTest>
        void WithPool(size_t workers, TTest&& test) {
            if (workers == kInlinePool) {
                ThreadPool pool(ThreadPool::InlineTag{});
                test(pool);
                pool.Shutdown();
                return;
            }
            ThreadPool pool(workers);
            test(pool);
            pool.Shutdown();
        }

        /// One counter per index, so a missed or doubled index shows
        class HitCounters {
        public:
            explicit HitCounters(size_t count) : _count(count), _hits(MakeUnique<std::atomic<uint32_t>[]>(count)) {}

            void Hit(size_t begin, size_t end) {
                for (size_t i = begin; i < end; ++i) {
                    _hits[i].fetch_add(1, std::memory_order_relaxed);
                }
            }

            [[nodiscard]] bool EveryIndexOnce() const {
                for (size_t i = 0; i < _count; ++i) {
                    if (_hits[i].load(std::memory_order_relaxed) != 1) {
                        return false;
                    }
                }
                return true;
            }

        private:
            size_t _count;
            UniquePtr<std::atomic<uint32_t>[]> _hits;
        };

        struct Range {
            size_t begin = 0;
            size_t end = 0;
            bool operator==(const Range&) const = default;
            bool operator<(const Range& other) const {
                return begin != other.begin ? begin < other.begin : end < other.end;
            }
        };

        /// The threads that ran a chunk, and the ranges they were given
        class ChunkLog {
        public:
            void Record(size_t begin, size_t end) {
                std::lock_guard lock(_mutex);
                _threads.push_back(std::this_thread::get_id());
                _ranges.push_back({begin, end});
            }

            [[nodiscard]] size_t DistinctThreads() const {
                std::lock_guard lock(_mutex);
                Vector<std::thread::id> unique = _threads;
                std::sort(unique.begin(), unique.end());
                return static_cast<size_t>(std::unique(unique.begin(), unique.end()) - unique.begin());
            }

            [[nodiscard]] bool RanOnlyOn(std::thread::id thread) const {
                std::lock_guard lock(_mutex);
                return std::all_of(_threads.begin(), _threads.end(), [thread](std::thread::id id) { return id == thread; });
            }

            /// The ranges ordered by their start
            [[nodiscard]] Vector<Range> SortedRanges() const {
                std::lock_guard lock(_mutex);
                Vector<Range> ranges = _ranges;
                std::sort(ranges.begin(), ranges.end());
                return ranges;
            }

        private:
            mutable std::mutex _mutex;
            Vector<std::thread::id> _threads;
            Vector<Range> _ranges;
        };

    }  // namespace

    TEST(ParallelForTest, EveryIndexIsVisitedExactlyOnce) {
        for (const size_t workers : kWorkerCounts) {
            WithPool(workers, [&](ThreadPool& pool) {
                for (const size_t count : {size_t{0}, size_t{1}, kMinChunk, kMinChunk + 1, kPrimeCount, kBigCount}) {
                    HitCounters hits(count);
                    ParallelFor(pool, count, kMinChunk, [&](size_t begin, size_t end) { hits.Hit(begin, end); });
                    EXPECT_TRUE(hits.EveryIndexOnce()) << workers << " workers, count " << count;
                }
            });
        }
    }

    TEST(ParallelForTest, TheTaskManagerForwardsToItsPool) {
        EngineEnvironment::EnsureCoreServices();
        HitCounters hits(kBigCount);
        TaskManager::GetInstance().ParallelFor(kBigCount, kMinChunk, [&](size_t begin, size_t end) { hits.Hit(begin, end); });
        EXPECT_TRUE(hits.EveryIndexOnce());
    }

    TEST(ParallelForTest, AnEmptyRangeCallsNoBody) {
        WithPool(2, [](ThreadPool& pool) {
            bool called = false;
            ParallelFor(pool, 0, kMinChunk, [&](size_t, size_t) { called = true; });
            EXPECT_FALSE(called);
        });
    }

    TEST(ParallelForTest, ChunksPartitionTheRangeAndRepeatFromRunToRun) {
        for (const size_t workers : kWorkerCounts) {
            WithPool(workers, [&](ThreadPool& pool) {
                Vector<Range> first;
                for (int run = 0; run < 3; ++run) {
                    ChunkLog log;
                    ParallelFor(pool, kPrimeCount, kMinChunk, [&](size_t begin, size_t end) { log.Record(begin, end); });
                    const Vector<Range> ranges = log.SortedRanges();

                    size_t expectedBegin = 0;
                    for (const Range& range : ranges) {
                        EXPECT_EQ(range.begin, expectedBegin) << workers << " workers";
                        EXPECT_GE(range.end - range.begin, kMinChunk) << workers << " workers";
                        expectedBegin = range.end;
                    }
                    EXPECT_EQ(expectedBegin, kPrimeCount) << workers << " workers";

                    if (run == 0) {
                        first = ranges;
                    } else {
                        EXPECT_TRUE(ranges == first) << workers << " workers";
                    }
                }
            });
        }
    }

    TEST(ParallelForTest, AFewIndicesStayOnTheCallerWithoutAJob) {
        WithPool(2, [](ThreadPool& pool) {
            ChunkLog log;
            ParallelFor(pool, 2 * kMinChunk - 1, kMinChunk, [&](size_t begin, size_t end) { log.Record(begin, end); });

            ASSERT_EQ(log.SortedRanges().size(), 1u);
            EXPECT_TRUE(log.RanOnlyOn(std::this_thread::get_id()));
            EXPECT_EQ(pool.GetPendingTaskCount(), 0u);
        });
    }

    TEST(ParallelForTest, TheInlinePoolRunsTheWholeRangeOnTheCaller) {
        WithPool(kInlinePool, [](ThreadPool& pool) {
            ChunkLog log;
            ParallelFor(pool, kBigCount, kMinChunk, [&](size_t begin, size_t end) { log.Record(begin, end); });

            const Vector<Range> ranges = log.SortedRanges();
            ASSERT_EQ(ranges.size(), 1u);
            EXPECT_EQ(ranges.front().begin, 0u);
            EXPECT_EQ(ranges.front().end, kBigCount);
            EXPECT_TRUE(log.RanOnlyOn(std::this_thread::get_id()));
        });
    }

    TEST(ParallelForTest, AStoppedPoolRunsTheWholeRangeOnTheCaller) {
        ThreadPool pool(2);
        pool.Shutdown();

        ChunkLog log;
        ParallelFor(pool, kBigCount, kMinChunk, [&](size_t begin, size_t end) { log.Record(begin, end); });

        ASSERT_EQ(log.SortedRanges().size(), 1u);
        EXPECT_TRUE(log.RanOnlyOn(std::this_thread::get_id()));
    }

    TEST(ParallelForTest, WorkersTakeChunksOfALargeRange) {
        for (const size_t workers : {size_t{1}, size_t{2}, size_t{4}}) {
            WithPool(workers, [&](ThreadPool& pool) {
                ChunkLog log;
                std::atomic<bool> gaveUp{false};
                // A chunk waits until a second thread has run one, so the check cannot pass by luck of scheduling: a pool that never lent a thread fails after the limit
                ParallelFor(pool, kBigCount, kMinChunk, [&](size_t begin, size_t end) {
                    log.Record(begin, end);
                    if (!WaitUntil([&] { return log.DistinctThreads() >= 2; })) {
                        gaveUp.store(true);
                    }
                });

                EXPECT_FALSE(gaveUp.load()) << workers << " workers";
                EXPECT_GE(log.DistinctThreads(), 2u) << workers << " workers";
            });
        }
    }

    TEST(ParallelForTest, ACallFromInsideAChunkFinishes) {
        for (const size_t workers : kWorkerCounts) {
            WithPool(workers, [&](ThreadPool& pool) {
                constexpr size_t kOuter = 512;
                constexpr size_t kInner = 4096;
                HitCounters hits(kOuter * kInner);
                ParallelFor(pool, kOuter, 8, [&](size_t begin, size_t end) {
                    for (size_t outer = begin; outer < end; ++outer) {
                        ParallelFor(pool, kInner, kMinChunk, [&](size_t innerBegin, size_t innerEnd) { hits.Hit(outer * kInner + innerBegin, outer * kInner + innerEnd); });
                    }
                });
                EXPECT_TRUE(hits.EveryIndexOnce()) << workers << " workers";
            });
        }
    }

    TEST(ParallelForTest, ACallFromAPoolJobFinishes) {
        ThreadPool pool(2);
        HitCounters hits(kBigCount);
        std::atomic<bool> done{false};
        pool.Submit([&] {
            ParallelFor(pool, kBigCount, kMinChunk, [&](size_t begin, size_t end) { hits.Hit(begin, end); });
            done.store(true, std::memory_order_release);
        });

        ASSERT_TRUE(WaitUntil([&] { return done.load(std::memory_order_acquire); }));
        EXPECT_TRUE(hits.EveryIndexOnce());
        pool.Shutdown();
    }

    TEST(ParallelForTest, BusyWorkersDoNotHoldACallUpAndLateHelpersTouchNothing) {
        constexpr size_t kWorkers = 2;
        constexpr int kCalls = 50;
        ThreadPool pool(kWorkers);

        std::atomic<bool> release{false};
        std::atomic<size_t> holding{0};
        for (size_t i = 0; i < kWorkers; ++i) {
            pool.Submit([&] {
                holding.fetch_add(1);
                while (!release.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            });
        }
        ASSERT_TRUE(WaitUntil([&] { return holding.load() == kWorkers; }));

        // Every worker is held: the helper jobs queue behind the long ones and the caller runs every chunk. The counter outlives the calls, the bodies do not
        std::atomic<size_t> bodyCalls{0};
        for (int call = 0; call < kCalls; ++call) {
            HitCounters hits(kPrimeCount);
            ChunkLog log;
            ParallelFor(pool, kPrimeCount, kMinChunk, [&](size_t begin, size_t end) {
                hits.Hit(begin, end);
                log.Record(begin, end);
                bodyCalls.fetch_add(1, std::memory_order_relaxed);
            });
            ASSERT_TRUE(hits.EveryIndexOnce());
            ASSERT_TRUE(log.RanOnlyOn(std::this_thread::get_id()));
        }
        const size_t callsWhileHeld = bodyCalls.load();

        // The queued helpers start now, find every chunk claimed and never call a body that is gone
        release.store(true, std::memory_order_release);
        pool.Shutdown();
        EXPECT_EQ(bodyCalls.load(), callsWhileHeld);
    }

}  // namespace BECore::Tests
