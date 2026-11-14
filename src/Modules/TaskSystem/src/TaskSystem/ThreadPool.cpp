#include <TaskSystem/ThreadPool.h>

namespace BECore {

#if BE_PROFILER
    namespace {

        // Counter names are string literals (Profiler/CLAUDE.md), so each graphed worker has its own
        constexpr const char* kWorkerBusyCounters[ThreadPool::kMaxGraphedWorkers] = {"Worker 0: busy", "Worker 1: busy", "Worker 2: busy", "Worker 3: busy"};

    }  // namespace
#endif

    ThreadPool::ThreadPool(size_t numThreads) {
        if (numThreads == 0) {
            numThreads = Thread::HardwareConcurrency();
            if (numThreads < 2) {
                numThreads = 2;
            }
        }

        _workerBusy = MakeUnique<std::atomic<bool>[]>(numThreads);

        // Создаем очереди для каждого потока
        _queues.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            _queues.push_back(MakeUnique<TaskQueue>());
        }

        // Запускаем рабочие потоки
        _threads.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            _threads.emplace_back(Format("Worker {}", i).c_str(), kWorkerSortBase + static_cast<uint32_t>(i), [this, i] { WorkerLoop(i); });
        }

        LOG_INFO("[ThreadPool] Created with {} threads"_format(numThreads));
    }

    ThreadPool::ThreadPool(InlineTag) : _isInline(true), _inlineThread(Thread::CurrentId()) {
        LOG_INFO("[ThreadPool] Created inline (no worker threads)");
    }

    ThreadPool::~ThreadPool() {
        Shutdown();
    }

    void ThreadPool::Submit(TaskQueue::TaskFunc task) {
        if (_stopped.load(std::memory_order_acquire)) {
            LOG_WARNING("[ThreadPool] Submitting task to stopped pool");
            return;
        }

        if (_isInline) {
            RunInline(std::move(task));
            return;
        }

        // Round-robin распределение по очередям
        size_t index = _nextQueue.fetch_add(1, std::memory_order_relaxed) % _queues.size();
        _queues[index]->Push(std::move(task));
    }

    void ThreadPool::SubmitToThread(size_t threadIndex, TaskQueue::TaskFunc task) {
        if (_stopped.load(std::memory_order_acquire)) {
            LOG_WARNING("[ThreadPool] Submitting task to stopped pool");
            return;
        }

        if (_isInline) {
            RunInline(std::move(task));
            return;
        }

        if (threadIndex >= _queues.size()) {
            threadIndex = threadIndex % _queues.size();
        }

        _queues[threadIndex]->Push(std::move(task));
    }

    void ThreadPool::RunInline(TaskQueue::TaskFunc task) {
        ASSERT(Thread::CurrentId() == _inlineThread, "The inline ThreadPool is used from the thread that created it only");

        if (_drainingInline) {
            // A job is running on this stack: the outer call runs this one after it.
            _inlineQueue.push_back(std::move(task));
            return;
        }

        _drainingInline = true;
        task();
        while (!_inlineQueue.empty()) {
            TaskQueue::TaskFunc next = std::move(_inlineQueue.front());
            _inlineQueue.pop_front();
            next();
        }
        _drainingInline = false;
    }

    void ThreadPool::Shutdown() {
        if (_stopped.exchange(true, std::memory_order_acq_rel)) {
            return;  // Already stopped
        }

        LOG_INFO("[ThreadPool] Shutting down...");

        // Останавливаем все очереди
        for (auto& queue : _queues) {
            queue->Stop();
        }

        // Ждем завершения всех потоков
        for (auto& thread : _threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }

        LOG_INFO("[ThreadPool] Shutdown complete");
    }

    size_t ThreadPool::GetPendingTaskCount() const {
        size_t count = _inlineQueue.size();
        for (const auto& queue : _queues) {
            count += queue->Size();
        }
        return count;
    }

    size_t ThreadPool::GetBusyWorkerCount() const {
        size_t count = 0;
        for (size_t i = 0; i < _threads.size(); ++i) {
            count += _workerBusy[i].load(std::memory_order_relaxed) ? 1 : 0;
        }
        return count;
    }

    void ThreadPool::SampleProfilerCounters() {
#if BE_PROFILER
        if (_isInline || Profiler::GetBackend() == nullptr) {
            return;
        }
        BE_PROFILE_COUNTER("Pool: pending jobs", GetPendingTaskCount());
        BE_PROFILE_COUNTER("Pool: steals", _stealCount.exchange(0, std::memory_order_relaxed));
#endif
    }

    void ThreadPool::SetWorkerBusy(size_t threadIndex, bool busy) {
#if BE_PROFILER
        if (Profiler::GetBackend() != nullptr) {
            // One lock around the flag and the points it moves, so the graphs get their values in the order they were true: two workers
            // that change at the same moment must not leave `Pool: busy workers` at the older value. Taken only while a backend records
            LockGuard lock(_busyReportMutex);
            _workerBusy[threadIndex].store(busy, std::memory_order_relaxed);
            if (threadIndex < kMaxGraphedWorkers) {
                BE_PROFILE_COUNTER(kWorkerBusyCounters[threadIndex], busy ? 1 : 0);
            }
            BE_PROFILE_COUNTER("Pool: busy workers", GetBusyWorkerCount());
            return;
        }
#endif
        _workerBusy[threadIndex].store(busy, std::memory_order_relaxed);
    }

    void ThreadPool::WorkerLoop(size_t threadIndex) {
        SetWorkerBusy(threadIndex, false);  // the first point of the graph of this worker
        TaskQueue::TaskFunc task;
        const auto run = [this, threadIndex, &task] {
            SetWorkerBusy(threadIndex, true);
            {
                BE_PROFILE_SCOPE("Task");
                task();
            }
            SetWorkerBusy(threadIndex, false);
        };

        while (!_stopped.load(std::memory_order_acquire)) {
            // Сначала пытаемся взять из своей очереди
            if (_queues[threadIndex]->TryPop(task)) {
                run();
                continue;
            }

            // Пытаемся украсть у других
            if (TryStealTask(threadIndex, task)) {
                _stealCount.fetch_add(1, std::memory_order_relaxed);
                run();
                continue;
            }

            // Ждем задачу в своей очереди; the wait is a zone, so a trace tells an idle worker from a busy one
            bool gotTask = false;
            {
                BE_PROFILE_SCOPE("Idle");
                gotTask = _queues[threadIndex]->Pop(task);
            }
            if (gotTask) {
                run();
            }
        }

        // Доделываем оставшиеся задачи в своей очереди
        while (_queues[threadIndex]->TryPop(task)) {
            run();
        }
    }

    bool ThreadPool::TryStealTask(size_t thiefIndex, TaskQueue::TaskFunc& task) {
        size_t numQueues = _queues.size();

        // Пытаемся украсть из других очередей
        for (size_t i = 1; i < numQueues; ++i) {
            size_t victimIndex = (thiefIndex + i) % numQueues;
            if (_queues[victimIndex]->TrySteal(task)) {
                return true;
            }
        }

        return false;
    }

}  // namespace BECore
