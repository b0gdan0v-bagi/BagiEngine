#include "EditorPlayModel.h"

namespace BECore {

    void EditorPlayModel::PressPlay() {
        if (_state == EditorPlayState::Stopped) {
            _state = EditorPlayState::Playing;
        } else {
            _state = EditorPlayState::Stopped;
        }
        _stepCount = 0;
    }

    void EditorPlayModel::PressPause() {
        if (_state == EditorPlayState::Playing) {
            _state = EditorPlayState::Paused;
        } else if (_state == EditorPlayState::Paused) {
            _state = EditorPlayState::Playing;
        }
    }

    void EditorPlayModel::PressStep() {
        if (_state == EditorPlayState::Paused) {
            ++_stepCount;
        }
    }

}  // namespace BECore
