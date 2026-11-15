#pragma once

#include <BECore/Audio/AudioSettings.h>
#include <BECore/Audio/AudioTypes.h>
#include <BECore/Audio/IAudioBackend.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    class AudioClip;

    /**
     * @brief The sound of the game (CoreManager::GetAudio()): plays clips on a fixed set of voices of an IAudioBackend, under bus and master volumes.
     *
     * A sound gets a free voice, or else the one that started first; the gain of a voice is its volume times its bus's times the master's, 0 when
     * either is muted, and a change of a bus or of the master reaches the sounds that play. Without a backend (none set, or one that did not open)
     * every call is a no-op and Play() gives an invalid handle. Main thread only. A frame that plays nothing allocates nothing.
     */
    class AudioManager {
    public:
        AudioManager() = default;
        ~AudioManager();

        AudioManager(const AudioManager&) = delete;
        AudioManager& operator=(const AudioManager&) = delete;

        /// Reads config/AudioConfig.xml (defaults when the config is missing); call before SetBackend()
        void Initialize();

        /// Takes @p settings as the config's (clamped to their ranges); call before SetBackend()
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

        AudioSettings _settings;
        IntrusivePtr<IAudioBackend> _backend;
        Vector<Voice> _voices;
        uint64_t _startedCount = 0;
    };

}  // namespace BECore
