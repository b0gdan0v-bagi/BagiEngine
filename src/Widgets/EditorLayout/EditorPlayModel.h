#pragma once

namespace BECore {

    CORE_ENUM(EditorPlayState, uint8_t, Stopped, Playing, Paused)

    /**
     * @brief The state behind the Play / Pause / Step buttons of the editor's main toolbar.
     *
     * The buttons of the toolbar are the model's view of the engine's GamePause, which stays the single truth: the widget writes
     * the model's result to it and copies the engine's flag back with SetPaused every frame (the control API can pause too). Pause
     * and Step are valid in every state, because the editor's game runs live (that is @c Stopped until editor-play-in-editor gives
     * Play its meaning); the model remembers whether it was Stopped or Playing, to return there on resume. Plain data without an
     * ImGui context, so a test can drive it.
     */
    class EditorPlayModel {
    public:
        /// Stopped goes to Playing; Playing or Paused go to Stopped (the Play button is Play / Stop)
        void PressPlay();
        /// Stopped or Playing go to Paused; Paused returns to the state it was paused from
        void PressPause();
        /// Counts one step while Paused (the state stays Paused). Returns whether a step is due; ignored otherwise
        bool PressStep();
        /// Copies the pause of the engine: a pause set elsewhere makes the model Paused, a resume elsewhere returns it to where it was
        void SetPaused(bool paused);

        [[nodiscard]] EditorPlayState GetState() const {
            return _state;
        }
        /// Steps taken since the last Play; Stopped resets it
        [[nodiscard]] uint32_t GetStepCount() const {
            return _stepCount;
        }
        [[nodiscard]] bool CanPause() const {
            return true;
        }
        [[nodiscard]] bool CanStep() const {
            return _state == EditorPlayState::Paused;
        }

    private:
        EditorPlayState _state = EditorPlayState::Stopped;
        /// What Paused returns to (never Paused)
        EditorPlayState _stateBeforePause = EditorPlayState::Stopped;
        uint32_t _stepCount = 0;
    };

}  // namespace BECore
