#include <TaskSystem/TaskScheduler.h>
#include <cmath>

namespace BECore {

    namespace {

        using Rep = TaskScheduler::Duration::rep;

        /// a + b without signed overflow: a sum that does not fit stays at the nearest end of the range.
        TaskScheduler::Duration SaturatingAdd(TaskScheduler::Duration a, TaskScheduler::Duration b) {
            const Rep x = a.count();
            const Rep y = b.count();
            if (y > 0 && x > std::numeric_limits<Rep>::max() - y) {
                return TaskScheduler::Duration::max();
            }
            if (y < 0 && x < std::numeric_limits<Rep>::min() - y) {
                return TaskScheduler::Duration::min();
            }
            return TaskScheduler::Duration(x + y);
        }

        TaskScheduler::TimePoint SteadyNow() {
            return TaskScheduler::Clock::now();
        }

    }  // namespace

    void TaskScheduler::Initialize(ThreadPool* threadPool) {
        ASSERT(threadPool != nullptr, "ThreadPool must not be null");
        _threadPool = threadPool;
        _stopped.store(false, std::memory_order_relaxed);
        _sliceDeadline = (TimePoint::max)();
        LOG_INFO("[TaskScheduler] Initialized");
    }

    void TaskScheduler::Schedule(TaskQueue::TaskFunc task, TaskPriority priority, ThreadType threadType) {
        if (_stopped.load(std::memory_order_relaxed)) {
            LOG_WARNING("[TaskScheduler] Scheduling task on stopped scheduler");
            return;
        }

        if (threadType == ThreadType::MainThread) {
            LockGuard lock(_mainThreadMutex);
            _mainThreadQueue.Push({std::move(task), priority});
        } else {
            DispatchToThreadPool(std::move(task), priority);
        }
    }

    void TaskScheduler::ScheduleDelayed(TaskQueue::TaskFunc task, Duration delay, TaskPriority priority, ThreadType threadType, DelayClock clock) {
        if (_stopped.load(std::memory_order_relaxed)) {
            LOG_WARNING("[TaskScheduler] Scheduling delayed task on stopped scheduler");
            return;
        }

        if (clock == DelayClock::GameTime) {
            LockGuard lock(_delayedMutex);
            const TimePoint executeTime = TimePoint{} + SaturatingAdd(_gameTime, delay);
            _gameDelayedTasks.Push({std::move(task), executeTime, priority, threadType});
            return;
        }

        const TimePoint executeTime(SaturatingAdd(Clock::now().time_since_epoch(), delay));

        LockGuard lock(_delayedMutex);
        _delayedTasks.Push({std::move(task), executeTime, priority, threadType});
    }

    void TaskScheduler::AdvanceGameTime(double seconds) {
        if (!std::isfinite(seconds) || seconds <= 0.0) {
            return;
        }

        // One conversion to Duration, rounded to the nearest nanosecond (a float frame delta such as
        // float(0.7) sits just below its decimal value and must not lose a tick); the sums are integers.
        // A step that is too large saturates.
        constexpr double kMaxSeconds = std::chrono::duration<double>(Duration::max()).count();
        const Duration step = seconds >= kMaxSeconds ? Duration::max() : std::chrono::round<Duration>(std::chrono::duration<double>(seconds));

        LockGuard lock(_delayedMutex);
        _gameTime = SaturatingAdd(_gameTime, step);
    }

    TaskScheduler::Duration TaskScheduler::GetGameTime() const {
        LockGuard lock(_delayedMutex);
        return _gameTime;
    }

    void TaskScheduler::ProcessMainThreadTasks() {
        Vector<PrioritizedTask> tasksToProcess;

        {
            LockGuard lock(_mainThreadMutex);
            tasksToProcess.swap(_mainThreadBatch);
            while (!_mainThreadQueue.Empty()) {
                tasksToProcess.push_back(_mainThreadQueue.Pop());
            }
        }

        // Outside the mutex: a task may schedule more tasks
        for (auto& pt : tasksToProcess) {
            if (pt.task) {
                BE_PROFILE_SCOPE("Main thread task");
                pt.task();
            }
        }

        // Cleared before it is given back, outside the mutex: the destructor of a job may run any code
        tasksToProcess.clear();
        LockGuard lock(_mainThreadMutex);
        if (_mainThreadBatch.capacity() < tasksToProcess.capacity()) {
            _mainThreadBatch.swap(tasksToProcess);
        }
    }

    void TaskScheduler::ProcessDelayedTasks() {
        auto now = Clock::now();
        Vector<DelayedTask> readyTasks;

        {
            LockGuard lock(_delayedMutex);
            readyTasks.swap(_delayedBatch);
            while (!_delayedTasks.Empty() && _delayedTasks.Top().executeTime <= now) {
                readyTasks.push_back(_delayedTasks.Pop());
            }

            const TimePoint gameNow = TimePoint{} + _gameTime;
            while (!_gameDelayedTasks.Empty() && _gameDelayedTasks.Top().executeTime <= gameNow) {
                readyTasks.push_back(_gameDelayedTasks.Pop());
            }
        }

        for (auto& dt : readyTasks) {
            Schedule(std::move(dt.task), dt.priority, dt.threadType);
        }

        readyTasks.clear();
        LockGuard lock(_delayedMutex);
        if (_delayedBatch.capacity() < readyTasks.capacity()) {
            _delayedBatch.swap(readyTasks);
        }
    }

    void TaskScheduler::ScheduleYielded(TaskQueue::TaskFunc task, ThreadType threadType) {
        if (!_stopped.load(std::memory_order_relaxed) && _threadPool && _threadPool->IsInline() && _threadPool->IsRunningInlineJob()) {
            LockGuard lock(_backgroundMutex);
            _backgroundSlices.PushBack(std::move(task));
            return;
        }

        Schedule(std::move(task), TaskPriority::Low, threadType);
    }

    size_t TaskScheduler::ProcessBackgroundSlices(Duration budget) {
        if (!_threadPool || !_threadPool->IsInline() || _stopped.load(std::memory_order_relaxed)) {
            return 0;
        }

        // A budget of zero or less gives a window that is spent at once: one slice per call
        const Duration window = budget < Duration::zero() ? Duration::zero() : budget;
        const TimePoint start = BudgetNow();
        _sliceDeadline = TimePoint(SaturatingAdd(start.time_since_epoch(), window));

        size_t sliceCount = 0;
        while (!_stopped.load(std::memory_order_relaxed)) {
            if (sliceCount > 0 && BudgetNow() >= _sliceDeadline) {
                break;
            }

            TaskQueue::TaskFunc slice;
            {
                LockGuard lock(_backgroundMutex);
                if (_backgroundSlices.Empty()) {
                    break;
                }
                slice = _backgroundSlices.PopFront();
            }

            _threadPool->Submit(std::move(slice));
            ++sliceCount;
        }

        _sliceDeadline = (TimePoint::min)();
        return sliceCount;
    }

    bool TaskScheduler::ShouldYield() const {
        if (_stopped.load(std::memory_order_relaxed) || !_threadPool || !_threadPool->IsInline() || !_threadPool->IsRunningInlineJob()) {
            return false;
        }
        return BudgetNow() >= _sliceDeadline;
    }

    TaskScheduler::TimePoint TaskScheduler::BudgetNow() const {
        return _budgetNow ? _budgetNow() : SteadyNow();
    }

    void TaskScheduler::SetBudgetClock(TimePoint (*now)()) {
        _budgetNow = now;
    }

    size_t TaskScheduler::GetBackgroundSliceCount() const {
        LockGuard lock(_backgroundMutex);
        return _backgroundSlices.Size();
    }

    void TaskScheduler::Shutdown() {
        _stopped.store(true, std::memory_order_relaxed);
        _sliceDeadline = (TimePoint::max)();

        {
            LockGuard lock(_mainThreadMutex);
            _mainThreadQueue.Clear();
        }

        {
            LockGuard lock(_backgroundMutex);
            _backgroundSlices.Clear();
        }

        {
            LockGuard lock(_delayedMutex);
            _delayedTasks.Clear();
            _gameDelayedTasks.Clear();
        }

        LOG_INFO("[TaskScheduler] Shutdown complete");
    }

    size_t TaskScheduler::GetMainThreadQueueSize() const {
        LockGuard lock(_mainThreadMutex);
        return _mainThreadQueue.Size();
    }

    size_t TaskScheduler::GetDelayedTaskCount() const {
        LockGuard lock(_delayedMutex);
        return _delayedTasks.Size() + _gameDelayedTasks.Size();
    }

    void TaskScheduler::DispatchToThreadPool(TaskQueue::TaskFunc task, TaskPriority priority) {
        if (_threadPool) {
            // TODO: учитывать приоритет при выборе очереди
            _threadPool->Submit(std::move(task));
        } else {
            LOG_ERROR("[TaskScheduler] No thread pool available");
        }
    }

}  // namespace BECore
