#pragma once

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// One level of `audio_state` and `audio_set`; `volume` is a double so the text of a float widened by the answer stays
    struct AudioLevelAnswer {
        BE_CLASS(AudioLevelAnswer)

        BE_REFLECT_FIELD bool muted = false;
        BE_REFLECT_FIELD double volume = 0.0;
    };

    /// The `levels` of `audio_state`, and the answer of `audio_set`
    struct AudioLevelsAnswer {
        BE_CLASS(AudioLevelsAnswer)

        BE_REFLECT_FIELD AudioLevelAnswer master;
        BE_REFLECT_FIELD AudioLevelAnswer music;
        BE_REFLECT_FIELD AudioLevelAnswer sfx;
        BE_REFLECT_FIELD AudioLevelAnswer ui;
    };

    /// The `music` of `audio_state`: no `clip` while no music is loaded
    struct AudioMusicAnswer {
        BE_CLASS(AudioMusicAnswer)

        BE_REFLECT_FIELD Optional<PoolString> clip;
        BE_REFLECT_FIELD bool fading = false;
        BE_REFLECT_FIELD bool loop = false;
        BE_REFLECT_FIELD bool playing = false;
        BE_REFLECT_FIELD double volume = 0.0;
    };

    /// A request of the `recent` list of `audio_state`
    struct AudioRecentPlay {
        BE_CLASS(AudioRecentPlay)

        BE_REFLECT_FIELD PoolString bus;
        BE_REFLECT_FIELD PoolString clip;
        BE_REFLECT_FIELD uint64_t index = 0;
        BE_REFLECT_FIELD bool played = false;
        BE_REFLECT_FIELD double volume = 0.0;
    };

    /// `audio_state`: no `backend` member without a backend (`voices` is 0 then)
    struct AudioStateAnswer {
        BE_CLASS(AudioStateAnswer)

        BE_REFLECT_FIELD Optional<PoolString> backend;
        BE_REFLECT_FIELD AudioLevelsAnswer levels;
        BE_REFLECT_FIELD AudioMusicAnswer music;
        BE_REFLECT_FIELD uint32_t playing = 0;
        BE_REFLECT_FIELD ArenaVector<AudioRecentPlay> recent;
        BE_REFLECT_FIELD uint64_t requested = 0;
        BE_REFLECT_FIELD uint64_t started = 0;
        BE_REFLECT_FIELD uint32_t voices = 0;
    };

}  // namespace BECore
