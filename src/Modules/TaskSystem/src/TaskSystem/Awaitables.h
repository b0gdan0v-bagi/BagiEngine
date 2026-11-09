#pragma once

#include <TaskSystem/TaskPriority.h>
#include <chrono>
#include <coroutine>

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

        bool await_ready() const noexcept {
            return false;
        }
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
        DelayClock clock = DelayClock::WallClock;

        bool await_ready() const noexcept {
            return delay.count() <= 0;
        }
        void await_suspend(std::coroutine_handle<> handle) noexcept;
        void await_resume() const noexcept {}
    };

    /**
     * YieldAwaiter - уступает выполнение другим задачам.
     * Always suspends. A background job of a single-thread build is parked in the cooperative queue and resumed by
     * TaskManager::Update within the frame budget; a main-thread job resumes in the next main-thread drain; on a
     * threaded build the continuation goes back to the pool.
     *
     * @example
     * co_await Yield();
     */
    struct YieldAwaiter {
        bool await_ready() const noexcept {
            return false;
        }
        void await_suspend(std::coroutine_handle<> handle) noexcept;
        void await_resume() const noexcept {}
    };

    /**
     * YieldIfNeededAwaiter - yields only when the frame budget of the running background job is spent.
     * The call for a long loop: on a threaded build await_ready is always true (a bool test, no suspension); in a
     * single-thread build the task suspends once its slice of the frame is spent and continues in Update, and it
     * never suspends before the first frame. Main-thread jobs are not budgeted.
     *
     * @example
     * for (const auto& item : items) {
     *     co_await YieldIfNeeded();
     *     Process(item);
     * }
     */
    struct YieldIfNeededAwaiter {
        bool await_ready() const noexcept;
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
    DelayAwaiter Delay(std::chrono::duration<Rep, Period> duration, ThreadType threadType = ThreadType::Background) {
        return {std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration), threadType};
    }

    /**
     * Задержка в миллисекундах.
     */
    inline DelayAwaiter DelayMs(int64_t milliseconds, ThreadType threadType = ThreadType::Background) {
        return Delay(std::chrono::milliseconds(milliseconds), threadType);
    }

    /**
     * Задержка в секундах.
     */
    inline DelayAwaiter DelaySec(double seconds, ThreadType threadType = ThreadType::Background) {
        return Delay(std::chrono::duration<double>(seconds), threadType);
    }

    /**
     * Delay on game time: counts the frame deltas fed by CoreManager::OnGameCycle (Time::GetDeltaTime(): the frame
     * clamp and the manual clock apply), not the wall clock. Resumes on the main thread by default, in the Update
     * of the frame in which the game time reaches the delay. A non-positive delay does not suspend.
     * Call it from the main thread when the run must be deterministic.
     */
    template <typename Rep, typename Period>
    DelayAwaiter DelayGame(std::chrono::duration<Rep, Period> duration, ThreadType threadType = ThreadType::MainThread) {
        return {std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration), threadType, DelayClock::GameTime};
    }

    /**
     * Game-time delay in milliseconds.
     */
    inline DelayAwaiter DelayGameMs(int64_t milliseconds, ThreadType threadType = ThreadType::MainThread) {
        return DelayGame(std::chrono::milliseconds(milliseconds), threadType);
    }

    /**
     * Game-time delay in seconds. Converted to nanoseconds with std::chrono::round, like TaskScheduler::AdvanceGameTime,
     * so a delay equal to the sum of the frame deltas is released on that frame. A NaN or non-positive delay does not
     * suspend; a huge one is clamped to the largest Duration. A decimal delay equal to a float frame delta
     * (e.g. 0.7) may be released one frame later.
     */
    inline DelayAwaiter DelayGameSec(double seconds, ThreadType threadType = ThreadType::MainThread) {
        using Duration = std::chrono::steady_clock::duration;
        constexpr double kMaxSeconds = std::chrono::duration<double>(Duration::max()).count();

        // !(seconds > 0) is true for NaN too
        if (!(seconds > 0.0)) {
            return {Duration::zero(), threadType, DelayClock::GameTime};
        }
        const Duration delay = seconds >= kMaxSeconds ? Duration::max() : std::chrono::round<Duration>(std::chrono::duration<double>(seconds));
        return {delay, threadType, DelayClock::GameTime};
    }

    /**
     * Уступает выполнение другим задачам.
     */
    inline YieldAwaiter Yield() {
        return {};
    }

    /**
     * Yields when the frame budget is spent (single-thread build), otherwise continues at once.
     */
    inline YieldIfNeededAwaiter YieldIfNeeded() {
        return {};
    }

}  // namespace BECore
