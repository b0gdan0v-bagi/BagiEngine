#pragma once

namespace BECore {

    /// Frame time of one step when nothing says how long it lasts: one frame at 60 fps
    inline constexpr double kDefaultStepSeconds = 1.0 / 60.0;

    /// What the game does in one frame, as GamePause::BeginFrame() decides it
    CORE_ENUM(FrameKind, uint8_t, Running, Stepped, Held)

    /// The decision of one frame. @c seconds is the frame time of a Stepped frame and zero for the other kinds
    struct FrameDecision {
        FrameKind kind = FrameKind::Running;
        double seconds = 0.0;
    };

    /**
     * @brief Whether the game runs, is paused, or advances by single steps.
     *
     * One instance lives in CoreManager (CoreManager::GetPause()) and is the single truth for every tool that stops the
     * game: the editor toolbar and hotkeys, the control API and the manual clock (a manual clock is a pause that only
     * the control API steps). Pure state: it knows no clock, no scene and no window; Application::RunFrame asks it once
     * per frame, before anything else, and acts on the answer (Time, tasks, tweens, the scene update).
     *
     * Every frame is one of three kinds:
     *  - Running: not paused. A normal frame.
     *  - Stepped: paused and a step was requested. The frame consumes one step and advances the game by its fixed seconds.
     *  - Held: paused, nothing requested. The window, the input polling and the drawing go on, the game does not.
     *
     * Steps are counted, not queued one by one: a request adds to the pending count and the seconds of the latest
     * request apply to every pending step. Pausing keeps the pending steps, so a step asked for just before a pause is
     * not lost; resuming drops them, so a later pause does not start with stale steps. Main thread only.
     */
    class GamePause {
    public:
        /// Pause or resume. Resuming a paused game drops the pending steps; pausing keeps them
        void SetPaused(bool paused);

        [[nodiscard]] bool IsPaused() const {
            return _paused;
        }

        /**
         * @brief Ask for @p count more stepped frames of @p seconds each.
         *
         * They are taken only while paused (a running game takes none). Zero adds nothing.
         */
        void RequestSteps(uint32_t count, double seconds = kDefaultStepSeconds);

        /// Steps requested and not yet taken
        [[nodiscard]] uint32_t GetPendingSteps() const {
            return _pendingSteps;
        }

        /// Seconds the next stepped frame advances by
        [[nodiscard]] double GetStepSeconds() const {
            return _stepSeconds;
        }

        /// Whether the next BeginFrame() is a Stepped one
        [[nodiscard]] bool IsStepDue() const {
            return _paused && _pendingSteps > 0;
        }

        /// Decide the frame that starts now; a Stepped frame uses up one pending step. Application::RunFrame calls it once per frame
        [[nodiscard]] FrameDecision BeginFrame();

    private:
        bool _paused = false;
        uint32_t _pendingSteps = 0;
        double _stepSeconds = kDefaultStepSeconds;
    };

}  // namespace BECore
