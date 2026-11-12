#include "EditorPlayModel.h"

namespace BECore {

    void EditorPlayModel::PressPlay() {
        if (_state == EditorPlayState::Stopped) {
            _state = EditorPlayState::Playing;
        } else {
            _state = EditorPlayState::Stopped;
        }
        _stateBeforePause = _state;
        _stepCount = 0;
    }

    void EditorPlayModel::PressPause() {
        SetPaused(_state != EditorPlayState::Paused);
    }

    bool EditorPlayModel::PressStep() {
        if (_state != EditorPlayState::Paused) {
            return false;
        }
        ++_stepCount;
        return true;
    }

    void EditorPlayModel::SetPaused(bool paused) {
        if (paused && _state != EditorPlayState::Paused) {
            _stateBeforePause = _state;
            _state = EditorPlayState::Paused;
        } else if (!paused && _state == EditorPlayState::Paused) {
            _state = _stateBeforePause;
        }
    }

}  // namespace BECore
