#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Threading/ConditionVariable.h>
#include <BECore/Threading/LockGuards.h>
#include <BECore/Threading/LockSite.h>
#include <BECore/Threading/Mutex.h>
#include <TaskSystem/TaskMemory.h>
#include <TaskSystem/TaskPriority.h>
#include <coroutine>
#include <expected>

namespace BECore {

    template <typename T>
    class Task;

    namespace Detail {

        /**
         * One-shot signal "the coroutine reached final_suspend". Owned by TaskHandle; the promise only keeps a
         * pointer to it. Waiters read an atomic flag, never the coroutine frame state the worker is writing.
         */
        class CompletionEvent {
        public:
            CompletionEvent() = default;

            CompletionEvent(const CompletionEvent&) = delete;
            CompletionEvent& operator=(const CompletionEvent&) = delete;

            /** Keeps the owner of this event alive until the task finishes; the owner may be destroyed right inside Signal(). */
            void SetKeepAlive(IntrusivePtr<RefCountedAtomic> owner) {
                LockGuard lock(_mutex);
                _keepAlive = std::move(owner);
            }

            /**
             * Marks the task as finished, wakes all waiters and drops the keep-alive reference.
             * Notifies under the mutex, so a waiter cannot return (and destroy the event) before the lock is released.
             * Nothing may touch the event after this returns: the keep-alive reference may have been the last one.
             */
            void Signal() {
                IntrusivePtr<RefCountedAtomic> keepAlive;
                {
                    LockGuard lock(_mutex);
                    _signaled.store(true, std::memory_order_release);
                    keepAlive = std::move(_keepAlive);
                    _condition.notify_all();
                }
            }

            /**
             * Takes the keep-alive reference away from a task that will never finish (see LiveTasks::AbandonAll).
             * The caller releases it outside its own locks. Empty if the task never started or has already finished,
             * because Signal() hands the reference back itself.
             */
            [[nodiscard]] IntrusivePtr<RefCountedAtomic> ReleaseKeepAlive() {
                LockGuard lock(_mutex);
                return std::move(_keepAlive);
            }

            /**
             * Runs @p interrupt and wakes the waiters unless the task has finished. One decision under the mutex of Signal(), so a task that
             * finishes at the same moment is either interrupted before it or not at all.
             * @return Whether @p interrupt ran
             */
            template <typename TInterrupt>
            bool InterruptUnlessSignaled(TInterrupt&& interrupt) {
                LockGuard lock(_mutex);
                if (_signaled.load(std::memory_order_acquire)) {
                    return false;
                }
                interrupt();
                _condition.notify_all();
                return true;
            }

            [[nodiscard]] bool IsSignaled() const noexcept {
                return _signaled.load(std::memory_order_acquire);
            }

            /** Blocks until the task has finished or interrupted() returns true. */
            template <typename Predicate>
            void WaitUntil(Predicate interrupted) {
                UniqueLock lock(_mutex);
                _condition.wait(lock, [&] { return _signaled.load(std::memory_order_acquire) || interrupted(); });
            }

        private:
            Mutex _mutex{BE_LOCK_SITE("Task completion")};
            ConditionVariable _condition;
            std::atomic<bool> _signaled{false};
            IntrusivePtr<RefCountedAtomic> _keepAlive;  // guarded by _mutex
        };

        /** The awaiter of final_suspend: signals the completion, then resumes the awaiting coroutine if there is one. */
        struct FinalAwaiter {
            bool await_ready() noexcept {
                return false;
            }

            template <typename PromiseType>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<PromiseType> h) noexcept {
                auto& promise = h.promise();

                // After Signal() a waiting thread may destroy the frame, so nothing is read from the promise past it.
                const std::coroutine_handle<> continuation = promise._continuation;
                if (promise._completion) {
                    promise._completion->Signal();
                }

                if (continuation) {
                    return continuation;
                }
                return std::noop_coroutine();
            }

            void await_resume() noexcept {}
        };

        template <typename T>
        struct TaskPromiseBase {
            // The coroutine frame comes from TaskMemory; the sized delete of a promise gets the frame size, which picks the class back
            static void* operator new(size_t size) {
                return TaskMemory::Allocate(size);
            }
            static void operator delete(void* ptr, size_t size) {
                TaskMemory::Deallocate(ptr, size);
            }

            TaskError _error = TaskError::None;
            std::coroutine_handle<> _continuation;
            CompletionEvent* _completion = nullptr;  // set by TaskHandle; null for tasks awaited via co_await

            auto initial_suspend() noexcept {
                return std::suspend_always{};
            }

            FinalAwaiter final_suspend() noexcept {
                return FinalAwaiter{};
            }

            void unhandled_exception() noexcept {
                _error = TaskError::Exception;
                ASSERT(false, "Unhandled exception in coroutine");
            }

            void SetContinuation(std::coroutine_handle<> continuation) noexcept {
                _continuation = continuation;
            }
        };

        template <typename T>
        struct TaskPromise : TaskPromiseBase<T> {
            Variant<std::monostate, T> _result;

            Task<T> get_return_object() noexcept;

            void return_value(T value) noexcept {
                _result.template emplace<T>(std::move(value));
            }

            std::expected<T, TaskError> GetResult() & {
                if (this->_error != TaskError::None) {
                    return std::unexpected(this->_error);
                }
                return std::get<T>(_result);
            }

            std::expected<T, TaskError> GetResult() && {
                if (this->_error != TaskError::None) {
                    return std::unexpected(this->_error);
                }
                return std::move(std::get<T>(_result));
            }
        };

        template <>
        struct TaskPromise<void> : TaskPromiseBase<void> {
            Task<void> get_return_object() noexcept;

            void return_void() noexcept {}

            std::expected<void, TaskError> GetResult() {
                if (this->_error != TaskError::None) {
                    return std::unexpected(this->_error);
                }
                return {};
            }
        };

    }  // namespace Detail

    /**
     * Coroutine return type: lazy (suspended at the start), move-only, owns its frame. Start it with
     * TaskManager::Run(...) or `co_await` it from another coroutine.
     *
     * @example
     * Task<int> ComputeAsync() {
     *     co_return 42;
     * }
     *
     * Task<void> DoWorkAsync() {
     *     auto result = co_await ComputeAsync();  // std::expected<int, TaskError>
     *     if (result) {
     *         LOG_INFO("Result: {}"_format(*result));
     *     }
     * }
     */
    template <typename T = void>
    class [[nodiscard]] Task {
    public:
        using promise_type = Detail::TaskPromise<T>;
        using handle_type = std::coroutine_handle<promise_type>;

        Task() noexcept = default;

        explicit Task(handle_type handle) noexcept : _handle(handle) {}

        ~Task() {
            if (_handle) {
                _handle.destroy();
            }
        }

        Task(Task&& other) noexcept : _handle(other._handle) {
            other._handle = nullptr;
        }

        Task& operator=(Task&& other) noexcept {
            if (this != &other) {
                if (_handle) {
                    _handle.destroy();
                }
                _handle = other._handle;
                other._handle = nullptr;
            }
            return *this;
        }

        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;

        bool await_ready() const noexcept {
            return _handle.done();
        }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
            _handle.promise().SetContinuation(continuation);
            return _handle;
        }

        decltype(auto) await_resume() {
            return std::move(_handle.promise()).GetResult();
        }

        [[nodiscard]] bool IsDone() const noexcept {
            return !_handle || _handle.done();
        }

        [[nodiscard]] bool IsValid() const noexcept {
            return static_cast<bool>(_handle);
        }

        /** Resumes the coroutine once, on the calling thread. */
        void Resume() {
            if (_handle && !_handle.done()) {
                _handle.resume();
            }
        }

        /**
         * Resumes the coroutine on the calling thread until it finishes. Not for a task suspended on the pool: it
         * would race with the pool (start it with TaskManager::Run and wait on the handle instead).
         */
        decltype(auto) GetResult() {
            while (!IsDone()) {
                _handle.resume();
            }
            return std::move(_handle.promise()).GetResult();
        }

        [[nodiscard]] handle_type GetHandle() const noexcept {
            return _handle;
        }

    private:
        handle_type _handle = nullptr;
    };

    namespace Detail {

        template <typename T>
        Task<T> TaskPromise<T>::get_return_object() noexcept {
            return Task<T>{std::coroutine_handle<TaskPromise<T>>::from_promise(*this)};
        }

        inline Task<void> TaskPromise<void>::get_return_object() noexcept {
            return Task<void>{std::coroutine_handle<TaskPromise<void>>::from_promise(*this)};
        }

    }  // namespace Detail

}  // namespace BECore
