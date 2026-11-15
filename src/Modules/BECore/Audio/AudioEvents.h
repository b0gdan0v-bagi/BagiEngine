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

    /// Asks CoreManager::GetAudio() to stop every sound, the music too
    struct StopAllSoundsEvent : public EventBase<StopAllSoundsEvent> {};

    /// Asks CoreManager::GetAudio() for AudioManager::PlayMusic()
    struct PlayMusicEvent : public EventBase<PlayMusicEvent> {
        PoolString clip;
        float volume = 1.0f;
        bool loop = true;
        float fadeSeconds = 0.0f;

        explicit PlayMusicEvent(PoolString c, float v = 1.0f, bool l = true, float f = 0.0f) : clip(c), volume(v), loop(l), fadeSeconds(f) {}
    };

    /// Asks CoreManager::GetAudio() for AudioManager::StopMusic()
    struct StopMusicEvent : public EventBase<StopMusicEvent> {
        float fadeSeconds = 0.0f;

        explicit StopMusicEvent(float f = 0.0f) : fadeSeconds(f) {}
    };

}  // namespace BECore::AudioEvents
