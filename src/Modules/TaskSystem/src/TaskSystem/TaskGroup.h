#pragma once

#include <TaskSystem/CancellationToken.h>
#include <TaskSystem/TaskHandle.h>

namespace BECore {

    /**
     * TaskGroup - группа связанных задач.
     *
     * Позволяет управлять несколькими задачами как единым целым:
     * - Массовая отмена
     * - Ожидание завершения всех
     * - Общий CancellationToken
     *
     * @example
     * TaskGroup group;
     * group.Run(Task1());
     * group.Run(Task2());
     * group.WaitAll();
     */
    class TaskGroup {
    public:
        TaskGroup() : _token(CancellationToken::Create()) {}
        ~TaskGroup() = default;

        // Non-copyable, non-movable (due to mutex)
        TaskGroup(const TaskGroup&) = delete;
        TaskGroup& operator=(const TaskGroup&) = delete;
        TaskGroup(TaskGroup&&) = delete;
        TaskGroup& operator=(TaskGroup&&) = delete;

        /**
         * Добавляет handle в группу.
         */
        void Add(IntrusivePtr<TaskHandleBase> handle) {
            LockGuard lock(_mutex);
            _handles.push_back(std::move(handle));
        }

        /**
         * Отменяет все задачи в группе.
         */
        void CancelAll() {
            _token.Cancel();

            LockGuard lock(_mutex);
            for (auto& handle : _handles) {
                handle->Cancel();
            }
        }

        /**
         * Ожидает завершения всех задач.
         */
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

        /**
         * Проверяет, завершены ли все задачи.
         */
        [[nodiscard]] bool AllDone() const {
            LockGuard lock(_mutex);
            for (const auto& handle : _handles) {
                if (!handle->IsDone()) {
                    return false;
                }
            }
            return true;
        }

        /**
         * Возвращает количество задач в группе.
         */
        [[nodiscard]] size_t Size() const {
            LockGuard lock(_mutex);
            return _handles.size();
        }

        /**
         * Возвращает количество завершенных задач.
         */
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

        /**
         * Получает токен отмены группы.
         */
        [[nodiscard]] CancellationToken& GetToken() {
            return _token;
        }
        [[nodiscard]] const CancellationToken& GetToken() const {
            return _token;
        }

        /**
         * Очищает список задач.
         */
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
