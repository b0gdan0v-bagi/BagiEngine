#pragma once

#include <TaskSystem/TaskPriority.h>

#include <coroutine>
#include <chrono>
#include <functional>

namespace BECore {

    // Forward declarations
    class TaskManager;

    /**
     * MainThreadAwaiter - переключение на главный поток.
     * 
     * @example
     * co_await SwitchToMainThread();
     */
    struct MainThreadAwaiter {
        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> handle) noexcept;
        void await_resume() const noexcept {}
    };

    /**
     * BackgroundAwaiter - переключение на фоновый поток.
     * 
     * @example
     * co_await SwitchToBackground();
     */
    struct BackgroundAwaiter {
        TaskPriority priority = TaskPriority::Normal;

        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) noexcept;
        void await_resume() const noexcept {}
    };

    /**
     * DelayAwaiter - задержка на указанное время.
     * 
     * @example
     * co_await Delay(std::chrono::milliseconds(100));
     */
    struct DelayAwaiter {
        std::chrono::steady_clock::duration delay;
        ThreadType threadType = ThreadType::Background;

        bool await_ready() const noexcept { return delay.count() <= 0; }
        void await_suspend(std::coroutine_handle<> handle) noexcept;
        void await_resume() const noexcept {}
    };

    /**
     * YieldAwaiter - уступает выполнение другим задачам.
     * 
     * @example
     * co_await Yield();
     */
    struct YieldAwaiter {
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) noexcept;
        void await_resume() const noexcept {}
    };

    // =================================================================
    // Factory functions
    // =================================================================

    /**
     * Переключает выполнение на главный поток.
     */
    inline MainThreadAwaiter SwitchToMainThread() {
        return {};
    }

    /**
     * Переключает выполнение на фоновый поток.
     */
    inline BackgroundAwaiter SwitchToBackground(TaskPriority priority = TaskPriority::Normal) {
        return {priority};
    }

    /**
     * Задержка выполнения.
     */
    template <typename Rep, typename Period>
    DelayAwaiter Delay(std::chrono::duration<Rep, Period> duration,
                       ThreadType threadType = ThreadType::Background) {
        return {std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration), threadType};
    }

    /**
     * Задержка в миллисекундах.
     */
    inline DelayAwaiter DelayMs(int64_t milliseconds,
                                 ThreadType threadType = ThreadType::Background) {
        return Delay(std::chrono::milliseconds(milliseconds), threadType);
    }

    /**
     * Задержка в секундах.
     */
    inline DelayAwaiter DelaySec(double seconds,
                                  ThreadType threadType = ThreadType::Background) {
        return Delay(std::chrono::duration<double>(seconds), threadType);
    }

    /**
     * Уступает выполнение другим задачам.
     */
    inline YieldAwaiter Yield() {
        return {};
    }

} // namespace BECore
