#include <TaskSystem/TaskScheduler.h>

namespace BECore {

    void TaskScheduler::Initialize(ThreadPool* threadPool) {
        ASSERT(threadPool != nullptr, "ThreadPool must not be null");
        _threadPool = threadPool;
        _stopped = false;
        LOG_INFO("[TaskScheduler] Initialized");
    }

    void TaskScheduler::Schedule(TaskQueue::TaskFunc task, 
                                  TaskPriority priority,
                                  ThreadType threadType) {
        if (_stopped) {
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

    void TaskScheduler::ScheduleDelayed(TaskQueue::TaskFunc task,
                                         Duration delay,
                                         TaskPriority priority,
                                         ThreadType threadType) {
        if (_stopped) {
            LOG_WARNING("[TaskScheduler] Scheduling delayed task on stopped scheduler");
            return;
        }

        auto executeTime = Clock::now() + delay;

        std::lock_guard lock(_delayedMutex);
        _delayedTasks.push({std::move(task), executeTime, priority, threadType});
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
        }

        // Планируем готовые задачи
        for (auto& dt : readyTasks) {
            Schedule(std::move(dt.task), dt.priority, dt.threadType);
        }
    }

    void TaskScheduler::Shutdown() {
        _stopped = true;

        // Очищаем очереди
        {
            std::lock_guard lock(_mainThreadMutex);
            while (!_mainThreadQueue.empty()) {
                _mainThreadQueue.pop();
            }
        }

        {
            std::lock_guard lock(_delayedMutex);
            while (!_delayedTasks.empty()) {
                _delayedTasks.pop();
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
        return _delayedTasks.size();
    }

    void TaskScheduler::DispatchToThreadPool(TaskQueue::TaskFunc task, TaskPriority priority) {
        if (_threadPool) {
            // TODO: учитывать приоритет при выборе очереди
            _threadPool->Submit(std::move(task));
        } else {
            LOG_ERROR("[TaskScheduler] No thread pool available");
        }
    }

} // namespace BECore
