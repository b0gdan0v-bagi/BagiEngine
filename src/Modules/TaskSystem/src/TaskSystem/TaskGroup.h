#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Threading/LockGuards.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Types/Types.h>
#include <TaskSystem/CancellationToken.h>
#include <TaskSystem/TaskHandle.h>
#include <cstddef>
#include <utility>

namespace BECore {

    /**
     * Tasks managed together: cancel all, wait for all, one shared CancellationToken. Thread-safe. The group holds
     * its handles until Clear() or its own destruction.
     *
     * @example
     * TaskGroup group;
     * group.Add(TaskManager::GetInstance().Run(Task1()));
     * group.Add(TaskManager::GetInstance().Run(Task2()));
     * group.WaitAll();
     */
    class TaskGroup {
    public:
        TaskGroup() : _token(CancellationToken::Create()) {}
        ~TaskGroup() = default;

        TaskGroup(const TaskGroup&) = delete;
        TaskGroup& operator=(const TaskGroup&) = delete;
        TaskGroup(TaskGroup&&) = delete;
        TaskGroup& operator=(TaskGroup&&) = delete;

        void Add(IntrusivePtr<TaskHandleBase> handle) {
            LockGuard lock(_mutex);
            _handles.push_back(std::move(handle));
        }

        /** Cancels the group's token and every handle in it. */
        void CancelAll() {
            _token.Cancel();

            LockGuard lock(_mutex);
            for (auto& handle : _handles) {
                handle->Cancel();
            }
        }

        /** Blocks until every task of the group at the time of the call has finished (a task added later is not waited for). */
        void WaitAll() {
            Vector<IntrusivePtr<TaskHandleBase>> handlesCopy;
            {
                LockGuard lock(_mutex);
                handlesCopy = _handles;
            }

            for (auto& handle : handlesCopy) {
                handle->Wait();
            }
        }

        [[nodiscard]] bool AllDone() const {
            LockGuard lock(_mutex);
            for (const auto& handle : _handles) {
                if (!handle->IsDone()) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] size_t Size() const {
            LockGuard lock(_mutex);
            return _handles.size();
        }

        [[nodiscard]] size_t CompletedCount() const {
            LockGuard lock(_mutex);
            size_t count = 0;
            for (const auto& handle : _handles) {
                if (handle->IsDone()) {
                    ++count;
                }
            }
            return count;
        }

        [[nodiscard]] CancellationToken& GetToken() {
            return _token;
        }
        [[nodiscard]] const CancellationToken& GetToken() const {
            return _token;
        }

        /** Drops the handles; the tasks themselves are not cancelled. */
        void Clear() {
            LockGuard lock(_mutex);
            _handles.clear();
        }

    private:
        Vector<IntrusivePtr<TaskHandleBase>> _handles;
        mutable Mutex _mutex{BE_LOCK_SITE("Task group")};
        CancellationToken _token;
    };

}  // namespace BECore
