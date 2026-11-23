#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <TaskSystem/LiveTasks.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskMemory.h>
#include <TaskSystem/TaskPriority.h>
#include <atomic>
#include <expected>
#include <type_traits>
#include <utility>

namespace BECore {

    /**
     * The type-erased part of TaskHandle, for containers (TaskGroup, LiveTasks). Reference-counted with an atomic
     * count: the handle may be released from any thread.
     */
    class TaskHandleBase : public RefCountedAtomic {
    public:
        virtual ~TaskHandleBase() = default;

        [[nodiscard]] virtual bool IsDone() const = 0;

        virtual void Cancel() = 0;

        virtual void Wait() = 0;

        /**
         * The coroutine finishes on its own thread without touching _status, so a stored Running that is already
         * done is reported as Completed here.
         */
        [[nodiscard]] TaskStatus GetStatus() const {
            const TaskStatus status = _status.load(std::memory_order_acquire);
            if (status == TaskStatus::Running && IsDone()) {
                return TaskStatus::Completed;
            }
            return status;
        }

        [[nodiscard]] TaskError GetError() const {
            return _error.load(std::memory_order_acquire);
        }

    protected:
        friend class Detail::LiveTasks;

        /**
         * The task is never going to run again (TaskManager::Shutdown): cancels it so waiters return, and hands back
         * the reference it holds on itself, if any. Only LiveTasks::AbandonAll calls it.
         */
        [[nodiscard]] virtual IntrusivePtr<RefCountedAtomic> Abandon() = 0;

        std::atomic<TaskStatus> _status{TaskStatus::Pending};
        std::atomic<TaskError> _error{TaskError::None};

        void SetStatus(TaskStatus status) {
            _status.store(status, std::memory_order_release);
        }

    private:
        // The links of Detail::LiveTasks' list, guarded by its mutex: registering a handle costs no allocation
        TaskHandleBase* _liveNext = nullptr;
        TaskHandleBase* _livePrev = nullptr;
    };

    /**
     * Owns a task and lets any thread track, wait for, cancel and read it. Cancelling is cooperative: the coroutine
     * is not interrupted, the handle only reads as cancelled and its waiters return.
     *
     * @example
     * auto handle = TaskManager::GetInstance().Run(MyTask());
     * auto result = handle->GetResult();  // std::expected<T, TaskError>
     */
    template <typename T = void>
    class TaskHandle : public TaskHandleBase {
    public:
        // From TaskMemory like the frames; a derived class has another size and gets the class that fits. The placement form is declared because a
        // class-scope operator new hides the global one (New)
        static void* operator new(size_t size) {
            return Detail::TaskMemory::Allocate(size);
        }
        static void* operator new(size_t, void* where) noexcept {
            return where;
        }
        static void operator delete(void* ptr, size_t size) {
            Detail::TaskMemory::Deallocate(ptr, size);
        }
        static void operator delete(void*, void*) noexcept {}

        TaskHandle() = default;

        explicit TaskHandle(Task<T>&& task) : _task(std::move(task)) {
            // The handle lives on the heap and never moves, so the promise can keep a pointer to _completion.
            if (_task.IsValid()) {
                _task.GetHandle().promise()._completion = &_completion;
            }
            SetStatus(TaskStatus::Pending);
        }

        ~TaskHandle() override = default;

        [[nodiscard]] bool IsDone() const override {
            return !_task.IsValid() || _completion.IsSignaled();
        }

        /**
         * Cancels the handle. A task that has already finished stays Completed and keeps its result.
         */
        void Cancel() override {
            _completion.InterruptUnlessSignaled([this] {
                TaskStatus status = _status.load(std::memory_order_acquire);
                while (status == TaskStatus::Pending || status == TaskStatus::Running) {
                    if (_status.compare_exchange_weak(status, TaskStatus::Cancelled, std::memory_order_acq_rel)) {
                        _cancelled.store(true, std::memory_order_release);
                        return;
                    }
                }
            });
        }

        /**
         * Blocks until the task has finished or the handle has been cancelled.
         * Never call it on the main thread for a task scheduled with ThreadType::MainThread: that queue is drained
         * in TaskManager::Update, so the wait would block itself.
         */
        void Wait() override {
            if (!_task.IsValid()) {
                return;
            }

            _completion.WaitUntil([this] { return IsCancelled(); });
        }

        /**
         * Blocks until the task has finished; the `Cancelled` error when the handle was cancelled. A copyable result can be read again and
         * from several threads; a move-only one is handed out by the first call.
         */
        std::expected<T, TaskError> GetResult() requires(!std::is_void_v<T>) {
            Wait();
            if (IsCancelled()) {
                return std::unexpected(TaskError::Cancelled);
            }
            if constexpr (std::is_copy_constructible_v<T>) {
                return _task.GetHandle().promise().GetResult();
            } else {
                return _task.GetResult();
            }
        }

        std::expected<void, TaskError> GetResult() requires(std::is_void_v<T>) {
            Wait();
            if (IsCancelled()) {
                return std::unexpected(TaskError::Cancelled);
            }
            return _task.GetResult();
        }

        [[nodiscard]] bool IsCancelled() const {
            return _cancelled.load(std::memory_order_acquire);
        }

        Task<T>& GetTask() {
            return _task;
        }
        const Task<T>& GetTask() const {
            return _task;
        }

        /**
         * From here until the coroutine finishes the handle keeps itself alive, so the caller may drop its
         * reference (fire and forget); the coroutine is resumed from pool threads that own nothing.
         * The caller of Start() must hold a reference of its own: the task may finish, and release the
         * last one, before Resume() returns.
         * Pending -> Running is one atomic step: a Cancel() that gets there first wins and the task never starts,
         * instead of Start() overwriting its Cancelled status.
         */
        void Start() {
            TaskStatus expected = TaskStatus::Pending;
            if (_status.compare_exchange_strong(expected, TaskStatus::Running, std::memory_order_acq_rel)) {
                _completion.SetKeepAlive(IntrusivePtr<RefCountedAtomic>(this));
                _task.Resume();
            }
        }

        void MarkFailed(TaskError error = TaskError::Exception) {
            _error.store(error, std::memory_order_release);
            SetStatus(TaskStatus::Failed);
        }

    protected:
        IntrusivePtr<RefCountedAtomic> Abandon() override {
            Cancel();  // a task that has already finished keeps its result
            return _completion.ReleaseKeepAlive();
        }

    private:
        // Declared before _task: the coroutine frame is destroyed first, the event it signals last.
        Detail::CompletionEvent _completion;
        Task<T> _task;
        std::atomic<bool> _cancelled{false};
        // Last on purpose: destroyed first, so the handle is out of LiveTasks before any member above goes away.
        Detail::LiveTaskRegistration _registration{*this};
    };

}  // namespace BECore
