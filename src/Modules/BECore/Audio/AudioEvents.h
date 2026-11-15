#pragma once

#include <BECore/Audio/AudioTypes.h>
#include <BECore/PoolString/PoolString.h>
#include <Events/EventBase.h>

namespace BECore::AudioEvents {

    /// Asks CoreManager::GetAudio() to play the clip at @c clip (a path ResourceManager loads); the sound of a game that does not hold the manager
    struct PlaySoundEvent : public EventBase<PlaySoundEvent> {
        PoolString clip;
        AudioBus bus = AudioBus::Sfx;
        float volume = 1.0f;

        explicit PlaySoundEvent(PoolString c, AudioBus b = AudioBus::Sfx, float v = 1.0f) : clip(c), bus(b), volume(v) {}
    };

    /// Asks CoreManager::GetAudio() to stop every sound
    struct StopAllSoundsEvent : public EventBase<StopAllSoundsEvent> {};

}  // namespace BECore::AudioEvents
