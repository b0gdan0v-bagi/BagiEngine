#pragma once

#include <BECore/Threading/Mutex.h>
#include <BECore/Types/Types.h>
#include <TaskSystem/MoveRing.h>
#include <TaskSystem/TaskPriority.h>
#include <TaskSystem/TaskQueue.h>
#include <TaskSystem/ThreadPool.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace BECore {

    namespace Detail {

        /// A binary heap (the largest element by operator< first) that moves its elements. eastl::priority_queue copies the new element in
        /// push_heap and gives only a const top(), so it cannot hold a task that owns a move-only capture.
        template <typename T>
        class MovableHeap {
        public:
            void Push(T value) {
                _items.push_back(std::move(value));
                std::push_heap(_items.begin(), _items.end());
            }

            /// The largest element
            [[nodiscard]] const T& Top() const {
                return _items.front();
            }

            /// Removes the largest element and returns it by move
            [[nodiscard]] T Pop() {
                std::pop_heap(_items.begin(), _items.end());
                T value = std::move(_items.back());
                _items.pop_back();
                return value;
            }

            [[nodiscard]] bool Empty() const {
                return _items.empty();
            }

            [[nodiscard]] size_t Size() const {
                return _items.size();
            }

            void Clear() {
                _items.clear();
            }

        private:
            Vector<T> _items;
        };

    }  // namespace Detail

    /**
     * Queues of the task system: the main-thread queue (by priority), delayed tasks on two clocks, hand-off of
     * background jobs to the ThreadPool, and the cooperative queue of the frame budget of a single-thread build
     * (inline ThreadPool): a background coroutine that yields is parked there and resumed by
     * ProcessBackgroundSlices for a set time per frame. Thread-safe except the frame-budget state, which only
     * the one thread of an inline pool touches.
     */
    class TaskScheduler {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        using Duration = Clock::duration;

        TaskScheduler() = default;
        ~TaskScheduler() = default;

        TaskScheduler(const TaskScheduler&) = delete;
        TaskScheduler& operator=(const TaskScheduler&) = delete;

        /** @param threadPool Not owned; runs the background jobs. */
        void Initialize(ThreadPool* threadPool);

        /** Queues the task: the main-thread queue for ThreadType::MainThread, the pool otherwise. Dropped with a warning once stopped. */
        void Schedule(TaskQueue::TaskFunc task, TaskPriority priority = TaskPriority::Normal, ThreadType threadType = ThreadType::Background);

        /**
         * Schedules the task after @p delay, released by ProcessDelayedTasks.
         * @param clock Real time (default) or game time (AdvanceGameTime). A game delay counts from the game time at
         *              the moment of the call; call it from the main thread for a deterministic release. A
         *              non-positive delay is ready at the next ProcessDelayedTasks.
         */
        void ScheduleDelayed(TaskQueue::TaskFunc task, Duration delay, TaskPriority priority = TaskPriority::Normal, ThreadType threadType = ThreadType::Background,
                             DelayClock clock = DelayClock::WallClock);

        /**
         * Advances the game clock, the only way to move DelayClock::GameTime. NaN, infinity and a negative step are
         * ignored; a step that is too large saturates. The step is rounded to the nearest nanosecond once, so the
         * sums are exact integers and equal on every platform.
         * @param seconds Step in seconds (in a frame: Time::GetDeltaTime())
         */
        void AdvanceGameTime(double seconds);

        /** The sum of the accepted AdvanceGameTime steps. */
        [[nodiscard]] Duration GetGameTime() const;

        /**
         * Runs the queued main-thread tasks, highest priority first. Call it from the main thread once per frame.
         */
        void ProcessMainThreadTasks();

        /**
         * Schedules the ready delayed tasks: real-time ones by Clock::now(), game-time ones by GetGameTime().
         * Call it regularly (TaskManager::Update does).
         */
        void ProcessDelayedTasks();

        /** Stops the scheduler: later Schedule calls are dropped and every queue is cleared (queued tasks are destroyed unrun). */
        void Shutdown();

        [[nodiscard]] size_t GetMainThreadQueueSize() const;

        [[nodiscard]] size_t GetDelayedTaskCount() const;

        /**
         * Schedules the continuation of a coroutine that yielded (YieldAwaiter, YieldIfNeededAwaiter).
         * With an inline pool, called from a background job: the continuation joins the cooperative queue and
         * is resumed by ProcessBackgroundSlices. Otherwise exactly Schedule(task, TaskPriority::Low, threadType),
         * so a main-thread yield resumes in the next main-thread drain and a yield on a worker pool goes back
         * to the pool.
         */
        void ScheduleYielded(TaskQueue::TaskFunc task, ThreadType threadType);

        /**
         * Resumes the parked continuations of background jobs (FIFO) until the queue is empty or the budget is
         * spent. A budget of zero or less still resumes one slice, so a parked task is never starved. A slice
         * that yields again is resumed again in the same call while time remains (round robin). The call
         * overruns the budget by at most one slice: a coroutine cannot be preempted, only the time between
         * two of its own checks counts. While it runs, ShouldYield() compares the budget clock with the
         * deadline of this call; after it the window counts as spent until the next call.
         * A no-op (returns 0) unless the pool is inline and the scheduler is not stopped. Call it from the
         * single thread, once per frame, after the main-thread tasks (TaskManager::Update does).
         * @param budget Time for this call, by the budget clock
         * @return Number of slices run
         */
        size_t ProcessBackgroundSlices(Duration budget);

        /**
         * True when the running background job should suspend (co_await YieldIfNeeded()): the pool is inline,
         * the caller is a background job and the budget window is spent. False on a worker pool (only the stop flag and the kind of the
         * pool are read, so it is safe from any thread), in a main-thread job, outside any job, before the
         * first frame (the window is open until the first ProcessBackgroundSlices), and once the scheduler is stopped
         * (its pool may be gone).
         */
        [[nodiscard]] bool ShouldYield() const;

        /**
         * Replaces the clock of the budget (ProcessBackgroundSlices and ShouldYield only; delayed tasks keep
         * Clock::now()). nullptr restores steady_clock::now. For tests: a fake clock makes them independent
         * of the wall time.
         */
        void SetBudgetClock(TimePoint (*now)());

        [[nodiscard]] size_t GetBackgroundSliceCount() const;

    private:
        struct DelayedTask {
            TaskQueue::TaskFunc task;
            TimePoint executeTime;  // game queue: TimePoint{} + game time
            TaskPriority priority;
            ThreadType threadType;

            bool operator<(const DelayedTask& other) const {
                // The heap puts the largest first, so the earliest time compares largest
                return executeTime > other.executeTime;
            }
        };

        struct PrioritizedTask {
            TaskQueue::TaskFunc task;
            TaskPriority priority;

            bool operator<(const PrioritizedTask& other) const {
                return static_cast<uint8_t>(priority) < static_cast<uint8_t>(other.priority);
            }
        };

        void DispatchToThreadPool(TaskQueue::TaskFunc task, TaskPriority priority);

        /// The clock of the budget: the one set by SetBudgetClock, else steady_clock::now.
        [[nodiscard]] TimePoint BudgetNow() const;

        ThreadPool* _threadPool = nullptr;

        // The three mutexes below have level 0: no path holds two of them (every guard is scoped and ProcessDelayedTasks schedules after it
        // released its lock), and Shutdown destroys queued handles under one of them, which takes the lock of LiveTasks
        Detail::MovableHeap<PrioritizedTask> _mainThreadQueue;
        // The storage of the batch a drain runs: taken out by ProcessMainThreadTasks under the mutex and given back, so a frame with tasks does not allocate a vector. A call
        // made while another one runs (a task that drains again) finds it empty and allocates its own
        Vector<PrioritizedTask> _mainThreadBatch;
        mutable Mutex _mainThreadMutex{BE_LOCK_SITE("Main thread queue")};

        // Delayed tasks by real time
        Detail::MovableHeap<DelayedTask> _delayedTasks;
        // Delayed tasks by game time (_gameTime), under the same mutex
        Detail::MovableHeap<DelayedTask> _gameDelayedTasks;
        Duration _gameTime{};
        // The same for ProcessDelayedTasks, under the same mutex
        Vector<DelayedTask> _delayedBatch;
        mutable Mutex _delayedMutex{BE_LOCK_SITE("Delayed tasks")};

        // Cooperative queue (inline pool only): continuations of background jobs that yielded
        Detail::MoveRing<TaskQueue::TaskFunc> _backgroundSlices;
        mutable Mutex _backgroundMutex{BE_LOCK_SITE("Cooperative queue")};

        // TimePoint max: no frame yet (never yield); now + budget: a drain runs; TimePoint min: spent
        TimePoint _sliceDeadline = (TimePoint::max)();
        TimePoint (*_budgetNow)() = nullptr;  // nullptr: steady_clock::now

        // Written by Initialize / Shutdown on the main thread, read by any thread (Schedule, ShouldYield)
        std::atomic<bool> _stopped{false};
    };

}  // namespace BECore
