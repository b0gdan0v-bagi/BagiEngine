#include "GamePause.h"

namespace BECore {

    void GamePause::SetPaused(bool paused) {
        if (_paused && !paused) {
            _pendingSteps = 0;
        }
        _paused = paused;
    }

    void GamePause::RequestSteps(uint32_t count, double seconds) {
        if (count == 0) {
            return;
        }
        _pendingSteps += count;
        _stepSeconds = seconds;
    }

    FrameDecision GamePause::BeginFrame() {
        if (!_paused) {
            return {FrameKind::Running, 0.0};
        }
        if (_pendingSteps == 0) {
            return {FrameKind::Held, 0.0};
        }
        --_pendingSteps;
        return {FrameKind::Stepped, _stepSeconds};
    }

}  // namespace BECore
