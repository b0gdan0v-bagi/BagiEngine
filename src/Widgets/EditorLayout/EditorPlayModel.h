#pragma once

namespace BECore {

    CORE_ENUM(EditorPlayState, uint8_t, Stopped, Playing, Paused)

    /**
     * @brief The state behind the Play / Pause / Step buttons of the editor's main toolbar.
     *
     * State only: nothing in the engine reacts to it yet (editor-play-in-editor and editor-pause will read it). Plain data
     * without an ImGui context, so a test can drive it.
     */
    class EditorPlayModel {
    public:
        /// Stopped goes to Playing; Playing or Paused go to Stopped (the Play button is Play / Stop)
        void PressPlay();
        /// Playing goes to Paused and Paused to Playing; ignored while Stopped
        void PressPause();
        /// Counts one step while Paused (the state stays Paused); ignored otherwise
        void PressStep();

        [[nodiscard]] EditorPlayState GetState() const {
            return _state;
        }
        /// Steps taken since the last Play; Stopped resets it
        [[nodiscard]] uint32_t GetStepCount() const {
            return _stepCount;
        }
        [[nodiscard]] bool CanPause() const {
            return _state != EditorPlayState::Stopped;
        }
        [[nodiscard]] bool CanStep() const {
            return _state == EditorPlayState::Paused;
        }

    private:
        EditorPlayState _state = EditorPlayState::Stopped;
        uint32_t _stepCount = 0;
    };

}  // namespace BECore
