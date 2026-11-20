#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Wait.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/TaskScheduler.h>
#include <TaskSystem/ThreadPool.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <iostream>

namespace BECore::Tests {

    namespace {

        constexpr uint64_t kSmokeTasks = 4000;
        constexpr uint64_t kBenchTasks = 200000;
        /// Tasks queued before one frame call (ProcessMainThreadTasks and the like): the per-frame costs are shared by this many tasks
        constexpr uint64_t kFrameTasks = 16;
        constexpr size_t kPoolWorkers = 4;
        constexpr double kNanosPerSecond = 1e9;
        /// Allocations of the process that no task made (gtest, the logger), tolerated on top of the budget of a route
        constexpr uint64_t kStrayAllocations = 16;
        /// A budget so large that the slices of a pass are never cut short
        constexpr std::chrono::seconds kUnlimitedSliceBudget{60};

        // Allocations of a route are the totals of the process: a route that runs on workers allocates on their threads too, and only the totals
        // see them. The test executable runs no other thread that allocates while a route runs; the tolerance above covers a stray allocation.

        // Allocations per task of each route: the measured baseline rounded up; every work package that removes a source lowers them, the last makes them 0.
        constexpr double kInlineSubmitAllocBudget = 0;
        constexpr double kPoolSubmitAllocBudget = 0;
        constexpr double kMainQueueAllocBudget = 0;
        constexpr double kDelayedAllocBudget = 0;
        constexpr double kRunCoroutineAllocBudget = 0;
        constexpr double kRunNestedAllocBudget = 0;
        constexpr double kYieldInlineAllocBudget = 0;

        struct RouteCost {
            uint64_t ran = 0;
            uint64_t allocations = 0;
            double nsPerTask = 0;
        };

        struct Route {
            const char* name;
            RouteCost (*measure)(uint64_t tasks);
            double allocBudget;
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
                for (uint64_t i = 0; i < count; ++i) {
                    pool.Submit([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });
                }
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

        Task<void> CountingTask(std::atomic<uint64_t>& ran) {
            ran.fetch_add(1, std::memory_order_relaxed);
            co_return;
        }

        Task<int> ChildTask() {
            co_return 1;
        }

        Task<void> ParentTask(std::atomic<uint64_t>& ran) {
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
                for (uint64_t i = 0; i < count; ++i) {
                    TaskManager::GetInstance().Run(makeTask(ran));
                }
                EXPECT_TRUE(WaitUntil([&] { return ran.load(std::memory_order_acquire) == count; }));
                return ran.load(std::memory_order_acquire);
            });
        }

        RouteCost MeasureRunCoroutine(uint64_t tasks) {
            return MeasureRun(tasks, [](std::atomic<uint64_t>& ran) { return CountingTask(ran); });
        }

        RouteCost MeasureRunNested(uint64_t tasks) {
            return MeasureRun(tasks, [](std::atomic<uint64_t>& ran) { return ParentTask(ran); });
        }

        constexpr Route kInlineSubmit{"inline-submit", MeasureInlineSubmit, kInlineSubmitAllocBudget};
        constexpr Route kPoolSubmit{"pool-submit", MeasurePoolSubmit, kPoolSubmitAllocBudget};
        constexpr Route kMainQueue{"main-queue", MeasureMainQueue, kMainQueueAllocBudget};
        constexpr Route kDelayed{"delayed", MeasureDelayed, kDelayedAllocBudget};
        constexpr Route kRunCoroutine{"run-coroutine", MeasureRunCoroutine, kRunCoroutineAllocBudget};
        constexpr Route kRunNested{"run-nested", MeasureRunNested, kRunNestedAllocBudget};
        constexpr Route kYieldInline{"yield-inline", MeasureYieldInline, kYieldInlineAllocBudget};
        constexpr const Route* kRoutes[] = {&kInlineSubmit, &kPoolSubmit, &kMainQueue, &kDelayed, &kRunCoroutine, &kRunNested, &kYieldInline};

        /// Every task ran; the allocations stay inside the budget of the route. Without the tracker the count check is skipped (the case reports it), the functional one is not.
        void ExpectRouteWithinBudget(const Route& route) {
            const RouteCost cost = route.measure(kSmokeTasks);
            EXPECT_EQ(cost.ran, kSmokeTasks) << route.name;
            if constexpr (AllocTracker::kEnabled) {
                EXPECT_LE(static_cast<double>(cost.allocations), route.allocBudget * static_cast<double>(kSmokeTasks) + static_cast<double>(kStrayAllocations))
                    << route.name << ": " << AllocationsPerTask(cost, kSmokeTasks) << " allocations per task";
            } else {
                GTEST_SKIP() << kNoAllocTracker;
            }
        }

    }  // namespace

    TEST(TaskAllocationTest, InlineSubmitWithinBudget) {
        ExpectRouteWithinBudget(kInlineSubmit);
    }

    TEST(TaskAllocationTest, PoolSubmitWithinBudget) {
        ExpectRouteWithinBudget(kPoolSubmit);
    }

    TEST(TaskAllocationTest, MainQueueWithinBudget) {
        ExpectRouteWithinBudget(kMainQueue);
    }

    TEST(TaskAllocationTest, DelayedWithinBudget) {
        ExpectRouteWithinBudget(kDelayed);
    }

    TEST(TaskAllocationTest, RunCoroutineWithinBudget) {
        ExpectRouteWithinBudget(kRunCoroutine);
    }

    TEST(TaskAllocationTest, RunNestedWithinBudget) {
        ExpectRouteWithinBudget(kRunNested);
    }

    TEST(TaskAllocationTest, YieldInlineWithinBudget) {
        ExpectRouteWithinBudget(kYieldInline);
    }

    /// The numbers of the perf set (task-submit-cost): one line per route. Run with --gtest_also_run_disabled_tests
    TEST(TaskAllocationTest, DISABLED_PrintsTheCostOfEveryRoute) {
        for (const Route* route : kRoutes) {
            const RouteCost cost = route->measure(kBenchTasks);
            EXPECT_EQ(cost.ran, kBenchTasks) << route->name;
            std::cout << "TaskAllocCost route=" << route->name << " allocs_per_task=" << AllocationsPerTask(cost, kBenchTasks) << " ns_per_task=" << cost.nsPerTask << "\n";
        }
    }

}  // namespace BECore::Tests
