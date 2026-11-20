#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Wait.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/TaskScheduler.h>
#include <TaskSystem/ThreadPool.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>
#include <thread>

namespace BECore::Tests {

    namespace {

        constexpr uint64_t kSmokeTasks = 4000;
        constexpr uint64_t kBenchTasks = 200000;
        /// Tasks queued before one frame call (ProcessMainThreadTasks and the like): the per-frame costs are shared by this many tasks
        constexpr uint64_t kFrameTasks = 16;
        constexpr size_t kPoolWorkers = 4;
        constexpr double kNanosPerSecond = 1e9;
        /// Allocations of the process that no task made (gtest, the logger), tolerated on top of the zero a route is held to
        constexpr uint64_t kStrayAllocations = 16;
        /// A budget so large that the slices of a pass are never cut short
        constexpr std::chrono::seconds kUnlimitedSliceBudget{60};

        // Allocations of a route are the totals of the process: a route that runs on workers allocates on their threads too, and only the totals
        // see them. The test executable runs no other thread that allocates while a route runs; the tolerance above covers a stray allocation.
        //
        // A route that runs on workers holds every job at a gate until the pass has queued all of them. Without it the number of jobs alive at once
        // (frames, handles, ring length) depends on how fast the workers drain, and a loaded machine makes a measured pass reach a higher peak than
        // the warm-up and grow the pools. With it both passes peak at the same count.

        /// Opened by the submitting thread after the last job of a pass is queued
        class Gate {
        public:
            void Open() {
                _open.store(true, std::memory_order_release);
            }
            void Wait() const {
                while (!_open.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            }

        private:
            std::atomic<bool> _open{false};
        };

        struct RouteCost {
            uint64_t ran = 0;
            uint64_t allocations = 0;
            double nsPerTask = 0;
        };

        struct Route {
            const char* name;
            RouteCost (*measure)(uint64_t tasks);
        };

        /// Runs @p pass (it runs the given number of tasks, waits for all of them and returns how many ran) twice: the first pass gives the pools and the rings their
        /// pages, the second is measured.
        template <typename TPass>
        RouteCost MeasureRoute(uint64_t tasks, TPass&& pass) {
            EXPECT_EQ(pass(tasks), tasks);

            const uint64_t allocationsBefore = AllocTracker::GetTotals().count;
            const auto start = std::chrono::steady_clock::now();
            RouteCost cost;
            cost.ran = pass(tasks);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            cost.allocations = AllocTracker::GetTotals().count - allocationsBefore;
            cost.nsPerTask = std::chrono::duration<double>(elapsed).count() * kNanosPerSecond / static_cast<double>(tasks);
            return cost;
        }

        double AllocationsPerTask(const RouteCost& cost, uint64_t tasks) {
            return static_cast<double>(cost.allocations) / static_cast<double>(tasks);
        }

        /// A scheduler of its own over an inline pool: no thread exists, the measuring thread does everything
        struct InlineSystem {
            InlineSystem() : pool(ThreadPool::InlineTag{}) {
                scheduler.Initialize(&pool);
            }
            ~InlineSystem() {
                scheduler.Shutdown();
                pool.Shutdown();
            }

            ThreadPool pool;
            TaskScheduler scheduler;
        };

        RouteCost MeasureInlineSubmit(uint64_t tasks) {
            InlineSystem system;
            return MeasureRoute(tasks, [&](uint64_t count) {
                uint64_t ran = 0;
                for (uint64_t i = 0; i < count; ++i) {
                    system.scheduler.Schedule([&ran] { ++ran; }, TaskPriority::Normal, ThreadType::Background);
                }
                return ran;
            });
        }

        RouteCost MeasurePoolSubmit(uint64_t tasks) {
            ThreadPool pool(kPoolWorkers);
            const RouteCost cost = MeasureRoute(tasks, [&](uint64_t count) {
                std::atomic<uint64_t> ran{0};
                Gate gate;
                for (uint64_t i = 0; i < count; ++i) {
                    pool.Submit([&ran, &gate] {
                        gate.Wait();
                        ran.fetch_add(1, std::memory_order_relaxed);
                    });
                }
                gate.Open();
                EXPECT_TRUE(WaitUntil([&] { return ran.load(std::memory_order_acquire) == count; }));
                return ran.load(std::memory_order_acquire);
            });
            pool.Shutdown();
            return cost;
        }

        RouteCost MeasureMainQueue(uint64_t tasks) {
            InlineSystem system;
            return MeasureRoute(tasks, [&](uint64_t count) {
                uint64_t ran = 0;
                for (uint64_t done = 0; done < count; done += kFrameTasks) {
                    for (uint64_t i = 0; i < kFrameTasks; ++i) {
                        system.scheduler.Schedule([&ran] { ++ran; }, TaskPriority::Normal, ThreadType::MainThread);
                    }
                    system.scheduler.ProcessMainThreadTasks();
                }
                return ran;
            });
        }

        RouteCost MeasureDelayed(uint64_t tasks) {
            InlineSystem system;
            return MeasureRoute(tasks, [&](uint64_t count) {
                uint64_t ran = 0;
                for (uint64_t done = 0; done < count; done += kFrameTasks) {
                    for (uint64_t i = 0; i < kFrameTasks; ++i) {
                        system.scheduler.ScheduleDelayed([&ran] { ++ran; }, TaskScheduler::Duration::zero(), TaskPriority::Normal, ThreadType::Background);
                    }
                    system.scheduler.ProcessDelayedTasks();
                }
                return ran;
            });
        }

        /// A job of the inline pool that parks its continuation, resumed by ProcessBackgroundSlices: what `co_await Yield()` does of a background job
        RouteCost MeasureYieldInline(uint64_t tasks) {
            InlineSystem system;
            return MeasureRoute(tasks, [&](uint64_t count) {
                uint64_t ran = 0;
                for (uint64_t done = 0; done < count; done += kFrameTasks) {
                    for (uint64_t i = 0; i < kFrameTasks; ++i) {
                        system.scheduler.Schedule([&system, &ran] { system.scheduler.ScheduleYielded([&ran] { ++ran; }, ThreadType::Background); }, TaskPriority::Normal, ThreadType::Background);
                    }
                    system.scheduler.ProcessBackgroundSlices(kUnlimitedSliceBudget);
                }
                return ran;
            });
        }

        Task<void> CountingTask(std::atomic<uint64_t>& ran, const Gate& gate) {
            gate.Wait();
            ran.fetch_add(1, std::memory_order_relaxed);
            co_return;
        }

        Task<int> ChildTask() {
            co_return 1;
        }

        Task<void> ParentTask(std::atomic<uint64_t>& ran, const Gate& gate) {
            gate.Wait();
            const std::expected<int, TaskError> value = co_await ChildTask();
            ran.fetch_add(static_cast<uint64_t>(value.value_or(0)), std::memory_order_relaxed);
            co_return;
        }

        /// The handle is dropped at once: a started task keeps itself alive, so what is counted is the work of Run and of the coroutine
        template <typename TMakeTask>
        RouteCost MeasureRun(uint64_t tasks, TMakeTask&& makeTask) {
            EngineEnvironment::EnsureCoreServices();
            return MeasureRoute(tasks, [&](uint64_t count) {
                std::atomic<uint64_t> ran{0};
                Gate gate;
                for (uint64_t i = 0; i < count; ++i) {
                    TaskManager::GetInstance().Run(makeTask(ran, gate));
                }
                gate.Open();
                EXPECT_TRUE(WaitUntil([&] { return ran.load(std::memory_order_acquire) == count; }));
                return ran.load(std::memory_order_acquire);
            });
        }

        RouteCost MeasureRunCoroutine(uint64_t tasks) {
            return MeasureRun(tasks, [](std::atomic<uint64_t>& ran, const Gate& gate) { return CountingTask(ran, gate); });
        }

        RouteCost MeasureRunNested(uint64_t tasks) {
            return MeasureRun(tasks, [](std::atomic<uint64_t>& ran, const Gate& gate) { return ParentTask(ran, gate); });
        }

        /// The start job of TaskManager::Run (MakeStartTask is private) on a pool of its own: the handle, the frame and the job are taken and given back by different threads
        RouteCost MeasureRunCoroutineOnFourWorkers(uint64_t tasks) {
            EngineEnvironment::EnsureCoreServices();
            ThreadPool pool(kPoolWorkers);
            TaskScheduler scheduler;
            scheduler.Initialize(&pool);
            const RouteCost cost = MeasureRoute(tasks, [&](uint64_t count) {
                std::atomic<uint64_t> ran{0};
                Gate gate;
                for (uint64_t i = 0; i < count; ++i) {
                    auto handle = MakeIntrusive<TaskHandle<void>>(CountingTask(ran, gate));
                    scheduler.Schedule(
                        [handle = std::move(handle)]() mutable {
                            auto owned = std::move(handle);
                            owned->Start();
                        },
                        TaskPriority::Normal, ThreadType::Background);
                }
                gate.Open();
                EXPECT_TRUE(WaitUntil([&] { return ran.load(std::memory_order_acquire) == count; }));
                return ran.load(std::memory_order_acquire);
            });
            scheduler.Shutdown();
            pool.Shutdown();
            return cost;
        }

        constexpr Route kInlineSubmit{"inline-submit", MeasureInlineSubmit};
        constexpr Route kPoolSubmit{"pool-submit", MeasurePoolSubmit};
        constexpr Route kMainQueue{"main-queue", MeasureMainQueue};
        constexpr Route kDelayed{"delayed", MeasureDelayed};
        constexpr Route kRunCoroutine{"run-coroutine", MeasureRunCoroutine};
        constexpr Route kRunNested{"run-nested", MeasureRunNested};
        constexpr Route kYieldInline{"yield-inline", MeasureYieldInline};
        constexpr Route kRunCoroutineOnFourWorkers{"run-coroutine-4-workers", MeasureRunCoroutineOnFourWorkers};
        constexpr const Route* kRoutes[] = {&kInlineSubmit, &kPoolSubmit, &kMainQueue, &kDelayed, &kRunCoroutine, &kRunNested, &kYieldInline, &kRunCoroutineOnFourWorkers};

        /// Every task ran and a steady route allocates nothing. Without the tracker the count check is skipped (the case reports it), the functional one is not.
        void ExpectRouteAllocatesNothing(const Route& route) {
            const RouteCost cost = route.measure(kSmokeTasks);
            EXPECT_EQ(cost.ran, kSmokeTasks) << route.name;
            if constexpr (AllocTracker::kEnabled) {
                EXPECT_LE(cost.allocations, kStrayAllocations) << route.name << ": " << AllocationsPerTask(cost, kSmokeTasks) << " allocations per task";
            } else {
                GTEST_SKIP() << kNoAllocTracker;
            }
        }

    }  // namespace

    TEST(TaskAllocationTest, InlineSubmitAllocatesNothing) {
        ExpectRouteAllocatesNothing(kInlineSubmit);
    }

    TEST(TaskAllocationTest, PoolSubmitAllocatesNothing) {
        ExpectRouteAllocatesNothing(kPoolSubmit);
    }

    TEST(TaskAllocationTest, MainQueueAllocatesNothing) {
        ExpectRouteAllocatesNothing(kMainQueue);
    }

    TEST(TaskAllocationTest, DelayedAllocatesNothing) {
        ExpectRouteAllocatesNothing(kDelayed);
    }

    TEST(TaskAllocationTest, RunCoroutineAllocatesNothing) {
        ExpectRouteAllocatesNothing(kRunCoroutine);
    }

    TEST(TaskAllocationTest, RunNestedAllocatesNothing) {
        ExpectRouteAllocatesNothing(kRunNested);
    }

    TEST(TaskAllocationTest, YieldInlineAllocatesNothing) {
        ExpectRouteAllocatesNothing(kYieldInline);
    }

    TEST(TaskAllocationTest, RunCoroutineOnFourWorkersAllocatesNothing) {
        ExpectRouteAllocatesNothing(kRunCoroutineOnFourWorkers);
    }

    /// A job above the inline buffer of a TaskFunc costs its one heap block and runs; a job with a small move-only capture costs none
    TEST(TaskAllocationTest, ACaptureAboveTheInlineBufferCostsOneAllocationAndStillRuns) {
        InlineSystem system;
        constexpr size_t kBigCaptureBytes = 100;
        struct BigCapture {
            uint8_t bytes[kBigCaptureBytes] = {};
        };
        static_assert(!kFunctionFitsInline<BigCapture>);

        uint64_t ran = 0;
        system.scheduler.Schedule([big = BigCapture{}, &ran] { ran += 1 + big.bytes[0]; }, TaskPriority::Normal, ThreadType::Background);  // warm up the queue
        ASSERT_EQ(ran, 1u);

        const uint64_t before = AllocTracker::GetThreadCounters().count;
        system.scheduler.Schedule([big = BigCapture{}, &ran] { ran += 1 + big.bytes[0]; }, TaskPriority::Normal, ThreadType::Background);
        const uint64_t bigCost = AllocTracker::GetThreadCounters().count - before;

        auto owned = MakeUnique<uint64_t>(1);
        const uint64_t beforeSmall = AllocTracker::GetThreadCounters().count;
        system.scheduler.Schedule([owned = std::move(owned), &ran] { ran += *owned; }, TaskPriority::Normal, ThreadType::Background);
        const uint64_t smallCost = AllocTracker::GetThreadCounters().count - beforeSmall;

        EXPECT_EQ(ran, 3u);
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(bigCost, 1u);
            EXPECT_EQ(smallCost, 0u);
        } else {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

    /// The numbers of the perf set (task-submit-cost): one line per route. Run with --gtest_also_run_disabled_tests
    TEST(TaskAllocationTest, DISABLED_PrintsTheCostOfEveryRoute) {
        for (const Route* route : kRoutes) {
            const RouteCost cost = route->measure(kBenchTasks);
            EXPECT_EQ(cost.ran, kBenchTasks) << route->name;
            std::cout << "TaskAllocCost route=" << route->name << " ns_per_task=" << cost.nsPerTask;
            if constexpr (AllocTracker::kEnabled) {
                std::cout << " allocs_per_task=" << AllocationsPerTask(cost, kBenchTasks);
            } else {
                std::cout << " allocs n/a";
            }
            std::cout << "\n";
        }
    }

}  // namespace BECore::Tests
