#include "AudioSettings.h"

#include <Generated/AudioSettings.gen.hpp>

namespace BECore {

    namespace {

        template <typename TSettings>
        auto& LevelOfBus(TSettings& settings, AudioBus bus) {
            switch (bus) {
                case AudioBus::Music:
                    return settings._music;
                case AudioBus::Ui:
                    return settings._ui;
                case AudioBus::Sfx:
                default:
                    return settings._sfx;
            }
        }

    }  // namespace

    AudioLevel& AudioSettings::LevelOf(AudioBus bus) {
        return LevelOfBus(*this, bus);
    }

    const AudioLevel& AudioSettings::LevelOf(AudioBus bus) const {
        return LevelOfBus(*this, bus);
    }

}  // namespace BECore
