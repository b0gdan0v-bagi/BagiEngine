#pragma once

#include <BECore/Audio/AudioSettings.h>
#include <BECore/Audio/AudioTypes.h>
#include <BECore/Audio/IAudioBackend.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <Events/SubscriptionHolder.h>
#include <cstdint>

namespace BECore {

    class AudioClip;

    namespace AudioEvents {
        struct PlaySoundEvent;
        struct StopAllSoundsEvent;
    }  // namespace AudioEvents

    /// How many play requests AudioManager remembers for GetRecentPlays()
    inline constexpr size_t kAudioRecentPlays = 32;

    /// One play request: what was asked and whether a voice took it
    struct AudioPlayRecord {
        uint64_t index = 0;  ///< the request's number, 1 for the first since the start (GetRequestCount())
        PoolString clip;     ///< the clip's path
        AudioBus bus = AudioBus::Sfx;
        float volume = 0.0f;  ///< as asked, clamped to 0 to 1
        bool played = false;  ///< false: no backend, a clip that did not load, or a backend that refused it
    };

    /**
     * @brief The sound of the game (CoreManager::GetAudio()): plays clips on a fixed set of voices of an IAudioBackend, under bus and master volumes.
     *
     * A sound gets a free voice, or else the one that started first; the gain of a voice is its volume times its bus's times the master's, 0 when
     * either is muted, and a change of a bus or of the master reaches the sounds that play. Without a backend (none set, or one that did not open)
     * every call is a no-op and Play() gives an invalid handle, but the request is still recorded (GetRecentPlays()), so a test or a control
     * client sees what a game asked for on a machine without sound. Main thread only. A frame that plays nothing allocates nothing.
     */
    class AudioManager : public SubscriptionHolder {
    public:
        AudioManager() = default;
        ~AudioManager() override;

        AudioManager(const AudioManager&) = delete;
        AudioManager& operator=(const AudioManager&) = delete;

        /// Reads config/AudioConfig.xml (defaults when the config is missing); call before SetBackend()
        void Initialize();

        /// Takes @p settings as the config's (clamped to their ranges) and handles the AudioEvents from now on; call before SetBackend()
        void Initialize(AudioSettings settings);

        /// Closes the old backend and opens @p backend with the configured voices; one that does not open is dropped with a warning
        void SetBackend(IntrusivePtr<IAudioBackend> backend);

        [[nodiscard]] const IntrusivePtr<IAudioBackend>& GetBackend() const {
            return _backend;
        }

        /// Stops every sound and closes the backend; SetBackend() may follow
        void Shutdown();

        /// Starts @p clip on @p bus at @p volume (0 to 1); an invalid handle when nothing plays (no backend, a failed clip)
        AudioHandle Play(const AudioClip& clip, AudioBus bus, float volume = 1.0f);

        /**
         * @brief Play() of the clip at @p clipPath, loaded through ResourceManager
         *
         * The load is synchronous the first time a path is played (the cache keeps the clip, also one that failed): preload the clips of a
         * scene (an AudioSourceComponent does, or ResourceManager::LoadAsync) so that a frame does not wait for a decode.
         */
        AudioHandle Play(PoolString clipPath, AudioBus bus, float volume = 1.0f);

        /// Stops the sound of @p handle; a stale handle is ignored
        void Stop(AudioHandle handle);

        void StopAll();

        [[nodiscard]] bool IsPlaying(AudioHandle handle) const;

        /// The settings in force: the config's, with every Set*() applied
        [[nodiscard]] const AudioSettings& GetSettings() const {
            return _settings;
        }

        /// Replaces the volumes and mutes (the backend name and the voice count are read only by SetBackend())
        void ApplySettings(const AudioSettings& settings);

        void SetMasterVolume(float volume);
        void SetMasterMuted(bool muted);
        void SetBusVolume(AudioBus bus, float volume);
        void SetBusMuted(AudioBus bus, bool muted);

        /// The gain a sound of @p volume on @p bus gets now
        [[nodiscard]] float GetGain(AudioBus bus, float volume) const;

        /// How many sounds Play() has started since the start: a sound leaves no other trace a test can wait for
        [[nodiscard]] uint64_t GetStartedCount() const {
            return _startedCount;
        }

        /// How many sounds Play() was asked for since the start, played or not
        [[nodiscard]] uint64_t GetRequestCount() const {
            return _requestCount;
        }

        /// The last kAudioRecentPlays requests, oldest first, into @p out (cleared first)
        void GetRecentPlays(Vector<AudioPlayRecord>& out) const;

        /// How many voices play now
        [[nodiscard]] uint32_t GetPlayingCount() const;

        /// Moves on every change of a volume or a mute (Set*(), ApplySettings()): a game saves the player's levels when it moved
        [[nodiscard]] uint64_t GetLevelsRevision() const {
            return _levelsRevision;
        }

    private:
        struct Voice {
            AudioBus bus = AudioBus::Sfx;
            float volume = 0.0f;
            uint32_t generation = 0;
            uint64_t startOrder = 0;  ///< 0: never started
        };

        [[nodiscard]] bool IsCurrent(AudioHandle handle) const;
        [[nodiscard]] uint32_t PickVoice() const;
        void RefreshGains();
        void Record(PoolString clip, AudioBus bus, float volume, bool played);
        void OnPlaySound(const AudioEvents::PlaySoundEvent& event);
        void OnStopAllSounds(const AudioEvents::StopAllSoundsEvent& event);

        AudioSettings _settings;
        IntrusivePtr<IAudioBackend> _backend;
        Vector<Voice> _voices;
        uint64_t _startedCount = 0;
        uint64_t _requestCount = 0;
        uint64_t _levelsRevision = 0;
        Array<AudioPlayRecord, kAudioRecentPlays> _recent;  ///< a ring: request n is at (n - 1) % kAudioRecentPlays
    };

}  // namespace BECore
