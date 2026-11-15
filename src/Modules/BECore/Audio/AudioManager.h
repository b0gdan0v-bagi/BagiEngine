#pragma once

#include <BECore/Audio/AudioDecode.h>
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
        struct PlayMusicEvent;
        struct StopMusicEvent;
    }  // namespace AudioEvents

    /// The voices of the music, after the voices of the config: two, so that one track fades out while the next fades in
    inline constexpr uint32_t kMusicVoices = 2;

    /// How far ahead of the device the music is decoded: what a frame that comes late does not hear
    inline constexpr float kMusicBufferSeconds = 0.5f;

    /// The frames of music decoded at a time
    inline constexpr size_t kMusicChunkFrames = 4096;

    /// The chunks one Update() decodes at most, so a long frame does not turn into a long decode
    inline constexpr uint32_t kMaxMusicChunksPerUpdate = 8;

    /// The music that plays now (the last PlayMusic()), for a menu or a test
    struct AudioMusicState {
        PoolString clip;  ///< empty: none
        bool playing = false;
        bool loop = false;
        float volume = 0.0f;
        bool fading = false;  ///< fading in, or out after StopMusic() with a fade
    };

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

        /// Closes the old backend and opens @p backend with the configured voices of the effects plus kMusicVoices; one that does not open is
        /// dropped with a warning
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
         * The load is synchronous the first time a path is played (the cache keeps the clip, also one whose file does not open or decode; a
         * path no loader takes is looked up and logged again at every call): preload the clips of a scene (an AudioSourceComponent does, or
         * ResourceManager::LoadAsync) so that a frame does not wait for a decode.
         */
        AudioHandle Play(PoolString clipPath, AudioBus bus, float volume = 1.0f);

        /**
         * @brief Plays the .ogg or .wav at @p clipPath as music on the Music bus, decoded while it plays (an Ogg file is read whole, a few
         * megabytes, and decoded a chunk at a time by Update(); a WAVE file is decoded whole)
         *
         * The track that plays fades out over @p fadeSeconds while this one fades in (0, or not a finite positive number: at once); a third
         * track stops the oldest. The same path as the music that plays only takes the new @p volume and @p loop, unless that track plays out
         * its end without a loop: then it starts again. False when nothing plays: no backend or one without
         * streams (NullAudioBackend), a file that does not open or decode. The file is read here, on the main thread.
         */
        bool PlayMusic(PoolString clipPath, float volume = 1.0f, bool loop = true, float fadeSeconds = 0.0f);

        /// Fades every music track out over @p fadeSeconds (0, or not a finite positive number: stops at once)
        void StopMusic(float fadeSeconds = 0.0f);

        [[nodiscard]] AudioMusicState GetMusicState() const;

        /**
         * @brief Once a frame (CoreManager, every kind of frame and the idle cycle): moves the fades by @p deltaSeconds and decodes the
         * music up to kMusicBufferSeconds ahead
         *
         * A frame time of 0 (a paused game, the manual clock between steps) holds the fades and still feeds the music. Allocates nothing.
         */
        void Update(float deltaSeconds);

        /// Stops the sound of @p handle; a stale handle is ignored
        void Stop(AudioHandle handle);

        /// Stops every sound and the music, at once
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

        /// How many voices of the sound effects play now (the music is GetMusicState())
        [[nodiscard]] uint32_t GetPlayingCount() const;

        /// The voices of the sound effects: the config's `voices` when a backend is open, else 0
        [[nodiscard]] uint32_t GetEffectVoiceCount() const {
            return static_cast<uint32_t>(_voices.size());
        }

        /// Moves on every change of a volume or a mute (Set*(), ApplySettings()): a game saves the player's levels when it moved
        [[nodiscard]] uint64_t GetLevelsRevision() const {
            return _levelsRevision;
        }

    private:
        struct MusicTrack {
            UniquePtr<IAudioSampleSource> source;  ///< null: the slot is free
            PoolString clip;
            uint32_t voice = 0;
            float volume = 1.0f;
            float fade = 1.0f;      ///< 0 to 1, a factor of the gain
            float fadeStep = 0.0f;  ///< per second; negative fades out and stops the track at 0
            uint64_t framesSinceRewind = 0;
            bool loop = true;
            bool ending = false;  ///< the source gave its last frame: the stream plays out
        };

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
        void OnPlayMusic(const AudioEvents::PlayMusicEvent& event);
        void OnStopMusic(const AudioEvents::StopMusicEvent& event);
        [[nodiscard]] float GetMusicGain(const MusicTrack& track) const;
        void FeedMusic(MusicTrack& track);
        void FadeOutMusic(MusicTrack& track, float fadeSeconds);
        void StopMusicTrack(MusicTrack& track);

        AudioSettings _settings;
        IntrusivePtr<IAudioBackend> _backend;
        Vector<Voice> _voices;
        uint64_t _startedCount = 0;
        uint64_t _requestCount = 0;
        uint64_t _levelsRevision = 0;
        Array<AudioPlayRecord, kAudioRecentPlays> _recent;  ///< a ring: request n is at (n - 1) % kAudioRecentPlays
        Array<MusicTrack, kMusicVoices> _music;             ///< track i plays on the music voice i, after the effect voices
        uint32_t _currentMusic = 0;                         ///< the track of the last PlayMusic(); the other one fades out or is free
        bool _hasMusicVoices = false;
        Vector<float> _musicScratch;  ///< one chunk of the widest stream, made at SetBackend()
    };

}  // namespace BECore
