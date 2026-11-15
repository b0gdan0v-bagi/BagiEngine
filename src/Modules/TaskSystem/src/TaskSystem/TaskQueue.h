#pragma once

#include <BECore/Threading/ConditionVariable.h>
#include <BECore/Threading/LockGuards.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <EASTL/deque.h>
#include <atomic>
#include <cstddef>
#include <utility>

namespace BECore {

    /**
     * Thread-safe FIFO of jobs with a blocking Pop. After Stop(), Pop still hands out what is queued and returns
     * false only when the queue is empty.
     */
    class TaskQueue {
    public:
        /// Move-only: a task owns its captures (a handle, a UniquePtr) and is moved from queue to queue, never copied.
        using TaskFunc = MoveOnlyFunction<void()>;

        TaskQueue() = default;
        ~TaskQueue() = default;

        TaskQueue(const TaskQueue&) = delete;
        TaskQueue& operator=(const TaskQueue&) = delete;
        TaskQueue(TaskQueue&&) = delete;
        TaskQueue& operator=(TaskQueue&&) = delete;

        void Push(TaskFunc task) {
            {
                LockGuard lock(_mutex);
                _tasks.push_back(std::move(task));
            }
            _condition.notify_one();
        }

        /** Blocks until a job is queued or the queue is stopped; false when stopped and empty. */
        bool Pop(TaskFunc& task) {
            UniqueLock lock(_mutex);
            _condition.wait(lock, [this] { return !_tasks.empty() || _stopped.load(std::memory_order_acquire); });

            if (_stopped.load(std::memory_order_acquire) && _tasks.empty()) {
                return false;
            }

            task = std::move(_tasks.front());
            _tasks.pop_front();
            return true;
        }

        /** Takes the oldest job without waiting. */
        bool TryPop(TaskFunc& task) {
            LockGuard lock(_mutex);
            if (_tasks.empty()) {
                return false;
            }
            task = std::move(_tasks.front());
            _tasks.pop_front();
            return true;
        }

        /** Takes the newest job without waiting (the other end from TryPop: work stealing). */
        bool TrySteal(TaskFunc& task) {
            LockGuard lock(_mutex);
            if (_tasks.empty()) {
                return false;
            }
            task = std::move(_tasks.back());
            _tasks.pop_back();
            return true;
        }

        void Stop() {
            {
                // The flag is part of the predicate of Pop's wait: it changes under the mutex, otherwise a
                // worker between the predicate check and the block misses the wakeup and sleeps forever.
                LockGuard lock(_mutex);
                _stopped.store(true, std::memory_order_release);
            }
            _condition.notify_all();
        }

        [[nodiscard]] bool IsStopped() const {
            return _stopped.load(std::memory_order_acquire);
        }

        [[nodiscard]] size_t Size() const {
            LockGuard lock(_mutex);
            return _tasks.size();
        }

        [[nodiscard]] bool Empty() const {
            LockGuard lock(_mutex);
            return _tasks.empty();
        }

        void Clear() {
            LockGuard lock(_mutex);
            _tasks.clear();
        }

    private:
        eastl::deque<TaskFunc> _tasks;
        mutable Mutex _mutex{BE_LOCK_SITE("Task queue")};
        ConditionVariable _condition;
        std::atomic<bool> _stopped{false};
    };

}  // namespace BECore
