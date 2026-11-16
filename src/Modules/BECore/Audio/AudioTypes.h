#pragma once

#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    /// The bus a sound plays on: each has its own volume and mute, under the master volume and mute
    CORE_ENUM(AudioBus, uint8_t, Sfx, Music, Ui)

    /// What decoded samples are: interleaved 32-bit floats, nominally -1 to 1, at this rate with this many channels
    struct AudioFormat {
        uint32_t sampleRate = 0;
        uint8_t channels = 0;

        [[nodiscard]] bool operator==(const AudioFormat&) const = default;
    };

    /// The channel counts a clip may have (mono to 7.1, what SDL plays)
    inline constexpr uint8_t kMaxAudioChannels = 8;

    /// The sample rates a clip may have; outside them a file is refused rather than resampled to nonsense
    inline constexpr uint32_t kMinAudioSampleRate = 1000;
    inline constexpr uint32_t kMaxAudioSampleRate = 384000;

    /// A sound started by Audio::Play(). It goes stale when its voice stops, ends or is taken by a later sound; a stale handle is ignored
    struct AudioHandle {
        static constexpr uint32_t kNoVoice = UINT32_MAX;

        uint32_t voice = kNoVoice;
        uint32_t generation = 0;

        [[nodiscard]] bool IsValid() const {
            return voice != kNoVoice;
        }
    };

}  // namespace BECore
