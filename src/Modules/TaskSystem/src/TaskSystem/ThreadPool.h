#pragma once

#include <TaskSystem/TaskQueue.h>

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
     *
     * Inline mode (ThreadPool(InlineTag{})): no thread is created; Submit runs the job on the calling
     * thread. It is the pool of a single-thread build (BE_SINGLE_THREAD, the browser) and is single-thread
     * by definition: it must be used from one thread only.
     */
    class ThreadPool {
    public:
        /// Selects the inline mode (see the class comment).
        struct InlineTag {};

        /// Workers that get a profiler graph of their own (`Worker n: busy`, named by literals): TaskManager::kMaxWorkerThreads must not exceed it
        static constexpr size_t kMaxGraphedWorkers = 4;
        /// The place of worker n in the list of threads of a trace: kWorkerSortBase + n (the main thread is 0, the control threads 200 and up)
        static constexpr uint32_t kWorkerSortBase = 100;

        /**
         * Создает пул потоков.
         * @param numThreads Количество потоков (0 = hardware_concurrency)
         */
        explicit ThreadPool(size_t numThreads = 0);

        /**
         * Creates an inline pool: GetThreadCount() is 0 and Submit / SubmitToThread run the job at once on
         * the calling thread, so a job is done when Submit returns. The one exception is a job submitted
         * while another inline job runs on this stack: it is queued and the outer call runs it (FIFO)
         * before it returns, so the recursion stays bounded.
         */
        explicit ThreadPool(InlineTag);

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
         * Returns true for an inline pool (no worker threads, jobs run on the submitting thread).
         */
        [[nodiscard]] bool IsInline() const {
            return _isInline;
        }

        /**
         * Возвращает количество рабочих потоков.
         */
        [[nodiscard]] size_t GetThreadCount() const {
            return _threads.size();
        }

        /**
         * Возвращает общее количество задач во всех очередях.
         */
        [[nodiscard]] size_t GetPendingTaskCount() const;

        /**
         * Returns the number of workers that run a job at this moment (0 for an inline pool).
         */
        [[nodiscard]] size_t GetBusyWorkerCount() const;

        /**
         * Sends the queues of the pool to the profiler as counters: `Pool: pending jobs` and `Pool: steals` (the steals since the last call).
         * Not for an inline pool. The busy graphs are not sampled: worker n reports `Worker n: busy` and `Pool: busy workers` itself every
         * time it starts or ends a job, so they are exact. Does nothing without a profiler backend or with BE_PROFILER=0.
         */
        void SampleProfilerCounters();

    private:
        void RunInline(TaskQueue::TaskFunc task);
        void WorkerLoop(size_t threadIndex);
        void SetWorkerBusy(size_t threadIndex, bool busy);
        bool TryStealTask(size_t thiefIndex, TaskQueue::TaskFunc& task);

        Vector<Thread> _threads;
        Vector<UniquePtr<TaskQueue>> _queues;
        std::atomic<bool> _stopped{false};
        std::atomic<size_t> _nextQueue{0};

        // One flag per worker (written by that worker, read by GetBusyWorkerCount) and the jobs taken from another worker's queue since the last sample
        UniquePtr<std::atomic<bool>[]> _workerBusy;
        Mutex _busyReportMutex{BE_LOCK_SITE("Pool busy report")};  // orders the busy graphs of the workers (only while a profiler backend records)
        std::atomic<uint64_t> _stealCount{0};

        // Inline mode only. One thread uses the pool, so no lock.
        bool _isInline = false;
        bool _drainingInline = false;
        Thread::Id _inlineThread;
        eastl::deque<TaskQueue::TaskFunc> _inlineQueue;
    };

}  // namespace BECore
