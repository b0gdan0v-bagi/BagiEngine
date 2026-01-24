#include <TaskSystem/ThreadPool.h>

namespace BECore {

    ThreadPool::ThreadPool(size_t numThreads) {
        if (numThreads == 0) {
            numThreads = std::thread::hardware_concurrency();
            if (numThreads < 2) {
                numThreads = 2;
            }
        }

        // Создаем очереди для каждого потока
        _queues.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            _queues.push_back(eastl::make_unique<TaskQueue>());
        }

        // Запускаем рабочие потоки
        _threads.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            _threads.emplace_back(&ThreadPool::WorkerLoop, this, i);
        }

        LOG_INFO("[ThreadPool] Created with {} threads"_format(numThreads));
    }

    ThreadPool::~ThreadPool() {
        Shutdown();
    }

    void ThreadPool::Submit(TaskQueue::TaskFunc task) {
        if (_stopped.load(std::memory_order_acquire)) {
            LOG_WARNING("[ThreadPool] Submitting task to stopped pool");
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

        if (threadIndex >= _queues.size()) {
            threadIndex = threadIndex % _queues.size();
        }

        _queues[threadIndex]->Push(std::move(task));
    }

    void ThreadPool::Shutdown() {
        if (_stopped.exchange(true, std::memory_order_acq_rel)) {
            return; // Already stopped
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
        size_t count = 0;
        for (const auto& queue : _queues) {
            count += queue->Size();
        }
        return count;
    }

    void ThreadPool::WorkerLoop(size_t threadIndex) {
        TaskQueue::TaskFunc task;

        while (!_stopped.load(std::memory_order_acquire)) {
            // Сначала пытаемся взять из своей очереди
            if (_queues[threadIndex]->TryPop(task)) {
                task();
                continue;
            }

            // Пытаемся украсть у других
            if (TryStealTask(threadIndex, task)) {
                task();
                continue;
            }

            // Ждем задачу в своей очереди
            if (_queues[threadIndex]->Pop(task)) {
                task();
            }
        }

        // Доделываем оставшиеся задачи в своей очереди
        while (_queues[threadIndex]->TryPop(task)) {
            task();
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

} // namespace BECore
