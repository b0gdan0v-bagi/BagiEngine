#pragma once

#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/CancellationToken.h>

#include <mutex>

#include <EASTL/vector.h>

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

        // Non-copyable, movable
        TaskGroup(const TaskGroup&) = delete;
        TaskGroup& operator=(const TaskGroup&) = delete;
        TaskGroup(TaskGroup&&) = default;
        TaskGroup& operator=(TaskGroup&&) = default;

        /**
         * Добавляет handle в группу.
         */
        void Add(IntrusivePtr<TaskHandleBase> handle) {
            std::lock_guard lock(_mutex);
            _handles.push_back(std::move(handle));
        }

        /**
         * Отменяет все задачи в группе.
         */
        void CancelAll() {
            _token.Cancel();

            std::lock_guard lock(_mutex);
            for (auto& handle : _handles) {
                handle->Cancel();
            }
        }

        /**
         * Ожидает завершения всех задач.
         */
        void WaitAll() {
            eastl::vector<IntrusivePtr<TaskHandleBase>> handlesCopy;
            {
                std::lock_guard lock(_mutex);
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
            std::lock_guard lock(_mutex);
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
            std::lock_guard lock(_mutex);
            return _handles.size();
        }

        /**
         * Возвращает количество завершенных задач.
         */
        [[nodiscard]] size_t CompletedCount() const {
            std::lock_guard lock(_mutex);
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
        [[nodiscard]] CancellationToken& GetToken() { return _token; }
        [[nodiscard]] const CancellationToken& GetToken() const { return _token; }

        /**
         * Очищает список задач.
         */
        void Clear() {
            std::lock_guard lock(_mutex);
            _handles.clear();
        }

    private:
        eastl::vector<IntrusivePtr<TaskHandleBase>> _handles;
        mutable std::mutex _mutex;
        CancellationToken _token;
    };

} // namespace BECore
