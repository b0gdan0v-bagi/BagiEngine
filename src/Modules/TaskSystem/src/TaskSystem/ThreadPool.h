#pragma once

#include <TaskSystem/TaskQueue.h>

#include <thread>
#include <atomic>

#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>

namespace BECore {

    /**
     * ThreadPool - пул рабочих потоков с work stealing.
     * 
     * Каждый поток имеет свою локальную очередь задач.
     * При отсутствии задач в локальной очереди поток пытается
     * украсть задачу из очередей других потоков.
     * 
     * @example
     * ThreadPool pool(4);  // 4 рабочих потока
     * pool.Submit([] { DoWork(); });
     */
    class ThreadPool {
    public:
        /**
         * Создает пул потоков.
         * @param numThreads Количество потоков (0 = hardware_concurrency)
         */
        explicit ThreadPool(size_t numThreads = 0);

        ~ThreadPool();

        // Non-copyable, non-movable
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;

        /**
         * Добавляет задачу в очередь.
         * @param task Функция для выполнения
         */
        void Submit(TaskQueue::TaskFunc task);

        /**
         * Добавляет задачу в очередь конкретного потока.
         * @param threadIndex Индекс потока
         * @param task Функция для выполнения
         */
        void SubmitToThread(size_t threadIndex, TaskQueue::TaskFunc task);

        /**
         * Останавливает все потоки.
         * Ожидает завершения текущих задач.
         */
        void Shutdown();

        /**
         * Проверяет, остановлен ли пул.
         */
        [[nodiscard]] bool IsStopped() const {
            return _stopped.load(std::memory_order_acquire);
        }

        /**
         * Возвращает количество рабочих потоков.
         */
        [[nodiscard]] size_t GetThreadCount() const { return _threads.size(); }

        /**
         * Возвращает общее количество задач во всех очередях.
         */
        [[nodiscard]] size_t GetPendingTaskCount() const;

    private:
        void WorkerLoop(size_t threadIndex);
        bool TryStealTask(size_t thiefIndex, TaskQueue::TaskFunc& task);

        eastl::vector<std::thread> _threads;
        eastl::vector<eastl::unique_ptr<TaskQueue>> _queues;
        std::atomic<bool> _stopped{false};
        std::atomic<size_t> _nextQueue{0};
    };

} // namespace BECore
