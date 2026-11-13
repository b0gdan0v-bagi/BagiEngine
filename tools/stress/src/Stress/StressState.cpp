#include "StressState.h"

namespace BECore::Stress {

    void StressState::SetScene(StressScene scene) {
        if (scene != _scene) {
            _scene = scene;
            ++_version;
        }
    }

    void StressState::SetCount(uint32_t count) {
        count = std::min(count, kMaxCount);
        if (count != _count) {
            _count = count;
            ++_version;
        }
    }

    void StressState::SetLanguage(StressLanguage language) {
        if (language != _language) {
            _language = language;
            ++_version;
        }
    }

}  // namespace BECore::Stress
