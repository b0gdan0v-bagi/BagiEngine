#pragma once

#include <BECore/Audio/AudioTypes.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// The volume and the mute of one bus (or of the master); a muted bus keeps its volume for when it is unmuted
    struct AudioLevel {
        BE_CLASS(AudioLevel)

        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 1)) float _volume = 1.0f;
        BE_REFLECT_FIELD bool _muted = false;
    };

    /// The voices a backend opens when the config says nothing; a sound that finds them all busy takes the oldest
    inline constexpr uint32_t kDefaultAudioVoices = 16;
    inline constexpr uint32_t kMaxAudioVoices = 64;

    /**
     * @brief The root of config/AudioConfig.xml: the defaults of the sound, which a game's file replaces whole.
     *
     * `backend` names the IAudioBackend the application fabric makes (empty: the platform's); `voices` how many sounds play at once; then the
     * master and the buses as child elements `<master volume muted/>`, `<sfx/>`, `<music/>`, `<ui/>`. The player's own choice is kept apart
     * (a later slice of no-audio), so this file stays the default.
     */
    struct AudioSettings {
        BE_CLASS(AudioSettings)

        BE_REFLECT_FIELD String _backend;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(1, 64)) uint32_t _voices = kDefaultAudioVoices;
        BE_REFLECT_FIELD AudioLevel _master;
        BE_REFLECT_FIELD AudioLevel _sfx;
        BE_REFLECT_FIELD AudioLevel _music;
        BE_REFLECT_FIELD AudioLevel _ui;

        [[nodiscard]] AudioLevel& LevelOf(AudioBus bus);
        [[nodiscard]] const AudioLevel& LevelOf(AudioBus bus) const;
    };

    // The generator reads `range` from the source text, so the bound of `_voices` is a literal: keep it the constant
    static_assert(kMaxAudioVoices == 64, "the range of AudioSettings::_voices spells kMaxAudioVoices");

}  // namespace BECore
