#pragma once

#include <chrono>

namespace BECore {

    class Application;

    /**
     * @brief Frame clock: how long the last frame took, how long the game has run and which fixed steps are due.
     *
     * One instance lives in CoreManager (CoreManager::GetTime()). Application::RunFrame calls Tick() once at the start
     * of every frame, before any event of that frame, so the values do not change while a frame is processed.
     * Scene::Update turns them into SceneFixedUpdateEvent (GetFixedStepCount() times) and SceneUpdateEvent.
     *
     * The clock is separate from the system clock on purpose: Advance() takes the frame time as a number, so a
     * test drives a component with any frame rate without waiting.
     *
     * Frame time is clamped to kMaxFrameTime, so a pause in a debugger or a window drag does not make the game
     * jump ahead and does not queue hundreds of fixed steps. The clamped value is what GetTotalTime() adds up.
     * A paused game holds the clock (Hold()) instead of advancing it.
     */
    class Time {
    public:
        using Clock = std::chrono::steady_clock;

        /// Fixed step of logic that must be deterministic, in seconds
        static constexpr double kDefaultFixedDeltaTime = 1.0 / 60.0;

        /// Longest frame the clock reports, in seconds
        static constexpr double kMaxFrameTime = 0.25;

        /**
         * @brief Advance by the real time since the previous Tick().
         *
         * The first call only starts the clock and advances by zero, so the time spent loading is not a frame.
         */
        void Tick(PassKey<Application>);

        /// Tick() at the moment @p now; tests pass their own, so the wall clock does not have to pass
        void TickAt(Clock::time_point now);

        /**
         * @brief Advance by @p seconds of frame time (negative values count as zero, long ones are clamped).
         *
         * Tests call it directly with a fake clock; a stepped frame of a paused game (GamePause) uses it too. It also
         * moves the wall-clock mark of Tick() to now when the clock has started, so a Tick() after the step measures
         * from the step and not from the last real frame.
         */
        void Advance(double seconds);

        /**
         * @brief A frame in which the game does not move (GamePause, Held): nothing advances.
         *
         * Total time and frame count stand still, the frame time and the fixed steps read zero (the fixed-step
         * remainder is kept), and the wall-clock mark of Tick() moves to now, so the first Tick() after the pause
         * measures one frame and not the time spent paused. @p now is the moment of the frame (tests pass their own).
         */
        void Hold(Clock::time_point now = Clock::now());

        /// Back to the state before the first frame; keeps the fixed step
        void Reset();

        /// Set the fixed step; a value that is not positive is ignored. Takes effect from the next Advance()
        void SetFixedDeltaTime(double seconds);

        /// Seconds the last frame took (clamped); zero before the first Advance()
        float GetDeltaTime() const;

        /// Seconds of game time since the first frame (sum of the clamped frame times)
        double GetTotalTime() const;

        /// Frames advanced so far; the first frame is 1
        uint64_t GetFrameCount() const;

        /// Length of one fixed step, in seconds
        float GetFixedDeltaTime() const;

        /// Fixed steps due in the current frame; the remainder (less than one step) carries over to the next frame
        uint32_t GetFixedStepCount() const;

    private:
        void AdvanceBy(double seconds);

        std::optional<Clock::time_point> _lastTick;
        double _fixedDeltaTime = kDefaultFixedDeltaTime;
        double _accumulator = 0.0;
        double _totalTime = 0.0;
        float _deltaTime = 0.0f;
        uint64_t _frameCount = 0;
        uint32_t _fixedStepCount = 0;
    };

}  // namespace BECore
