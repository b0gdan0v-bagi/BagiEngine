#pragma once

#include <BECore/Threading/ConditionVariable.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Threading/Thread.h>
#include <BECore/Types/Types.h>
#include <TaskSystem/TaskQueue.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /**
     * Worker threads with a queue each; a worker with an empty queue steals from the others, and an idle worker is woken by a job of any
     * queue, so a job never waits behind a long one while a worker is free. Submit is thread-safe.
     *
     * @example
     * ThreadPool pool(4);
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

        /** @param numThreads 0 = hardware_concurrency (at least 2). */
        explicit ThreadPool(size_t numThreads = 0);

        /**
         * Creates an inline pool: GetThreadCount() is 0 and Submit / SubmitToThread run the job at once on
         * the calling thread, so a job is done when Submit returns. The one exception is a job submitted
         * while another inline job runs on this stack: it is queued and the outer call runs it (FIFO)
         * before it returns, so the recursion stays bounded.
         */
        explicit ThreadPool(InlineTag);

        ~ThreadPool();

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;
        ThreadPool(ThreadPool&&) = delete;
        ThreadPool& operator=(ThreadPool&&) = delete;

        /** Round-robin over the worker queues; dropped with a warning once stopped. Inline pool: runs the job at once. */
        void Submit(TaskQueue::TaskFunc task);

        /** Like Submit, into the queue of one worker (the index wraps around the worker count); the others may still steal it. */
        void SubmitToThread(size_t threadIndex, TaskQueue::TaskFunc task);

        /** Idempotent. Joins the workers, each of which first runs what is left in its own queue. */
        void Shutdown();

        [[nodiscard]] bool IsStopped() const {
            return _stopped.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool IsInline() const {
            return _isInline;
        }

        [[nodiscard]] size_t GetThreadCount() const {
            return _threads.size();
        }

        [[nodiscard]] size_t GetPendingTaskCount() const;

        [[nodiscard]] size_t GetBusyWorkerCount() const;

        /**
         * Sends the pending-job and steal counters to the profiler (the steals since the last call). Not for an inline
         * pool. The busy graphs are not sampled: each worker reports them itself when it starts or ends a job.
         * Does nothing without a profiler backend.
         */
        void SampleProfilerCounters();

    private:
        void RunInline(TaskQueue::TaskFunc task);
        void WorkerLoop(size_t threadIndex);
        void AnnounceWork();
        void WaitForWork(uint64_t seenWork);
        void SetWorkerBusy(size_t threadIndex, bool busy);
        bool TryStealTask(size_t thiefIndex, TaskQueue::TaskFunc& task);

        Vector<Thread> _threads;
        Vector<UniquePtr<TaskQueue>> _queues;
        std::atomic<bool> _stopped{false};
        std::atomic<size_t> _nextQueue{0};

        // Moves after every push, under _idleMutex: a worker that read it before scanning the queues cannot sleep through a job it did not see
        std::atomic<uint64_t> _workEpoch{0};
        Mutex _idleMutex{BE_LOCK_SITE("Pool idle")};
        ConditionVariable _workAvailable;

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
