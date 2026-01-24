#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>

#include <EASTL/deque.h>

namespace BECore {

    /**
     * TaskQueue - потокобезопасная очередь задач.
     * 
     * Использует mutex для синхронизации и condition_variable для ожидания.
     * Поддерживает graceful shutdown.
     */
    class TaskQueue {
    public:
        using TaskFunc = std::function<void()>;

        TaskQueue() = default;
        ~TaskQueue() = default;

        // Non-copyable, non-movable
        TaskQueue(const TaskQueue&) = delete;
        TaskQueue& operator=(const TaskQueue&) = delete;
        TaskQueue(TaskQueue&&) = delete;
        TaskQueue& operator=(TaskQueue&&) = delete;

        /**
         * Добавляет задачу в очередь.
         * @param task Задача для выполнения
         */
        void Push(TaskFunc task) {
            {
                std::lock_guard lock(_mutex);
                _tasks.push_back(std::move(task));
            }
            _condition.notify_one();
        }

        /**
         * Извлекает задачу из очереди (блокирующе).
         * @param task Выходной параметр для задачи
         * @return true если задача получена, false если очередь остановлена
         */
        bool Pop(TaskFunc& task) {
            std::unique_lock lock(_mutex);
            _condition.wait(lock, [this] {
                return !_tasks.empty() || _stopped.load(std::memory_order_acquire);
            });

            if (_stopped.load(std::memory_order_acquire) && _tasks.empty()) {
                return false;
            }

            task = std::move(_tasks.front());
            _tasks.pop_front();
            return true;
        }

        /**
         * Пытается извлечь задачу без блокировки.
         * @param task Выходной параметр для задачи
         * @return true если задача получена
         */
        bool TryPop(TaskFunc& task) {
            std::lock_guard lock(_mutex);
            if (_tasks.empty()) {
                return false;
            }
            task = std::move(_tasks.front());
            _tasks.pop_front();
            return true;
        }

        /**
         * Пытается украсть задачу с конца очереди (для work stealing).
         * @param task Выходной параметр для задачи
         * @return true если задача получена
         */
        bool TrySteal(TaskFunc& task) {
            std::lock_guard lock(_mutex);
            if (_tasks.empty()) {
                return false;
            }
            task = std::move(_tasks.back());
            _tasks.pop_back();
            return true;
        }

        /**
         * Останавливает очередь и пробуждает все ожидающие потоки.
         */
        void Stop() {
            _stopped.store(true, std::memory_order_release);
            _condition.notify_all();
        }

        /**
         * Проверяет, остановлена ли очередь.
         */
        [[nodiscard]] bool IsStopped() const {
            return _stopped.load(std::memory_order_acquire);
        }

        /**
         * Возвращает количество задач в очереди.
         */
        [[nodiscard]] size_t Size() const {
            std::lock_guard lock(_mutex);
            return _tasks.size();
        }

        /**
         * Проверяет, пуста ли очередь.
         */
        [[nodiscard]] bool Empty() const {
            std::lock_guard lock(_mutex);
            return _tasks.empty();
        }

        /**
         * Очищает очередь.
         */
        void Clear() {
            std::lock_guard lock(_mutex);
            _tasks.clear();
        }

    private:
        eastl::deque<TaskFunc> _tasks;
        mutable std::mutex _mutex;
        std::condition_variable _condition;
        std::atomic<bool> _stopped{false};
    };

} // namespace BECore
