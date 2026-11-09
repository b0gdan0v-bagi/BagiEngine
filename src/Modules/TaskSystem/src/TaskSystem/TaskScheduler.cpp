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
            std::lock_guard lock(_mainThreadMutex);
            _mainThreadQueue.push({std::move(task), priority});
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
            std::lock_guard lock(_delayedMutex);
            const TimePoint executeTime = TimePoint{} + SaturatingAdd(_gameTime, delay);
            _gameDelayedTasks.push({std::move(task), executeTime, priority, threadType});
            return;
        }

        auto executeTime = Clock::now() + delay;

        std::lock_guard lock(_delayedMutex);
        _delayedTasks.push({std::move(task), executeTime, priority, threadType});
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

        std::lock_guard lock(_delayedMutex);
        _gameTime = SaturatingAdd(_gameTime, step);
    }

    TaskScheduler::Duration TaskScheduler::GetGameTime() const {
        std::lock_guard lock(_delayedMutex);
        return _gameTime;
    }

    void TaskScheduler::ProcessMainThreadTasks() {
        // Обрабатываем задачи из очереди главного потока
        eastl::vector<PrioritizedTask> tasksToProcess;

        {
            std::lock_guard lock(_mainThreadMutex);
            // Перемещаем все задачи для обработки
            while (!_mainThreadQueue.empty()) {
                tasksToProcess.push_back(std::move(const_cast<PrioritizedTask&>(_mainThreadQueue.top())));
                _mainThreadQueue.pop();
            }
        }

        // Выполняем задачи вне мьютекса
        for (auto& pt : tasksToProcess) {
            if (pt.task) {
                pt.task();
            }
        }
    }

    void TaskScheduler::ProcessDelayedTasks() {
        auto now = Clock::now();
        eastl::vector<DelayedTask> readyTasks;

        {
            std::lock_guard lock(_delayedMutex);
            while (!_delayedTasks.empty() && _delayedTasks.top().executeTime <= now) {
                readyTasks.push_back(std::move(const_cast<DelayedTask&>(_delayedTasks.top())));
                _delayedTasks.pop();
            }

            const TimePoint gameNow = TimePoint{} + _gameTime;
            while (!_gameDelayedTasks.empty() && _gameDelayedTasks.top().executeTime <= gameNow) {
                readyTasks.push_back(std::move(const_cast<DelayedTask&>(_gameDelayedTasks.top())));
                _gameDelayedTasks.pop();
            }
        }

        // Планируем готовые задачи
        for (auto& dt : readyTasks) {
            Schedule(std::move(dt.task), dt.priority, dt.threadType);
        }
    }

    void TaskScheduler::ScheduleYielded(TaskQueue::TaskFunc task, ThreadType threadType) {
        if (!_stopped.load(std::memory_order_relaxed) && _threadPool && _threadPool->IsInline() && _inBackgroundJob) {
            std::lock_guard lock(_backgroundMutex);
            _backgroundSlices.push_back(std::move(task));
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
                std::lock_guard lock(_backgroundMutex);
                if (_backgroundSlices.empty()) {
                    break;
                }
                slice = std::move(_backgroundSlices.front());
                _backgroundSlices.pop_front();
            }

            SubmitInlineBackgroundJob(std::move(slice));
            ++sliceCount;
        }

        _sliceDeadline = (TimePoint::min)();
        return sliceCount;
    }

    bool TaskScheduler::ShouldYield() const {
        if (_stopped.load(std::memory_order_relaxed) || !_threadPool || !_threadPool->IsInline() || !_inBackgroundJob) {
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
        std::lock_guard lock(_backgroundMutex);
        return _backgroundSlices.size();
    }

    void TaskScheduler::Shutdown() {
        _stopped.store(true, std::memory_order_relaxed);
        _sliceDeadline = (TimePoint::max)();

        // Очищаем очереди
        {
            std::lock_guard lock(_mainThreadMutex);
            while (!_mainThreadQueue.empty()) {
                _mainThreadQueue.pop();
            }
        }

        {
            std::lock_guard lock(_backgroundMutex);
            _backgroundSlices.clear();
        }

        {
            std::lock_guard lock(_delayedMutex);
            while (!_delayedTasks.empty()) {
                _delayedTasks.pop();
            }
            while (!_gameDelayedTasks.empty()) {
                _gameDelayedTasks.pop();
            }
        }

        LOG_INFO("[TaskScheduler] Shutdown complete");
    }

    size_t TaskScheduler::GetMainThreadQueueSize() const {
        std::lock_guard lock(_mainThreadMutex);
        return _mainThreadQueue.size();
    }

    size_t TaskScheduler::GetDelayedTaskCount() const {
        std::lock_guard lock(_delayedMutex);
        return _delayedTasks.size() + _gameDelayedTasks.size();
    }

    void TaskScheduler::DispatchToThreadPool(TaskQueue::TaskFunc task, TaskPriority priority) {
        if (_threadPool && _threadPool->IsInline()) {
            SubmitInlineBackgroundJob(std::move(task));
        } else if (_threadPool) {
            // TODO: учитывать приоритет при выборе очереди
            _threadPool->Submit(std::move(task));
        } else {
            LOG_ERROR("[TaskScheduler] No thread pool available");
        }
    }

    void TaskScheduler::SubmitInlineBackgroundJob(TaskQueue::TaskFunc task) {
        _threadPool->Submit([this, task = std::move(task)]() {
            // Restores the previous value: a drain can run a job while another one is on the stack
            const bool wasInBackgroundJob = _inBackgroundJob;
            _inBackgroundJob = true;
            task();
            _inBackgroundJob = wasInBackgroundJob;
        });
    }

}  // namespace BECore
