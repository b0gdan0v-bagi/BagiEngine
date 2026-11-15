#include "AudioManager.h"

#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioClipLoader.h>
#include <BECore/Audio/AudioEvents.h>
#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Resource/ResourceManager.h>
#include <cmath>

namespace BECore {

    namespace {

        constexpr StringView kConfigName = "AudioConfig";

        /// A volume from a caller or a file: 0 to 1, and 0 for NaN, so a gain is never out of range
        float ClampVolume(float volume) {
            if (!(volume > 0.0f)) {
                return 0.0f;
            }
            return volume > 1.0f ? 1.0f : volume;
        }

        void ClampLevel(AudioLevel& level) {
            level._volume = ClampVolume(level._volume);
        }

        void ClampSettings(AudioSettings& settings) {
            settings._voices = eastl::clamp(settings._voices, uint32_t{1}, kMaxAudioVoices);
            ClampLevel(settings._master);
            ClampLevel(settings._sfx);
            ClampLevel(settings._music);
            ClampLevel(settings._ui);
        }

    }  // namespace

    AudioManager::~AudioManager() {
        Shutdown();
    }

    void AudioManager::Initialize() {
        AudioSettings settings;
        if (auto deserializer = CoreManager::GetConfigManager().CreateDeserializer(kConfigName)) {
            settings.Deserialize(*deserializer);
            deserializer->FinishLoad();
            LogDeserializeErrors(kConfigName, *deserializer);
        }
        Initialize(std::move(settings));
    }

    void AudioManager::Initialize(AudioSettings settings) {
        _settings = std::move(settings);
        ClampSettings(_settings);
        ++_levelsRevision;
        // A second Initialize subscribes again: drop the old connections first so the holder keeps two
        UnsubscribeAll();
        Subscribe<AudioEvents::PlaySoundEvent, &AudioManager::OnPlaySound>(this);
        Subscribe<AudioEvents::StopAllSoundsEvent, &AudioManager::OnStopAllSounds>(this);
        Subscribe<AudioEvents::PlayMusicEvent, &AudioManager::OnPlayMusic>(this);
        Subscribe<AudioEvents::StopMusicEvent, &AudioManager::OnStopMusic>(this);
    }

    void AudioManager::SetBackend(IntrusivePtr<IAudioBackend> backend) {
        Shutdown();
        if (!backend) {
            return;
        }
        if (!backend->Open(_settings._voices + kMusicVoices)) {
            LOG_WARNING("Audio: the backend did not open an output, the game runs without sound");
            return;
        }
        _backend = std::move(backend);
        const uint32_t count = _backend->GetVoiceCount();
        _hasMusicVoices = count > kMusicVoices;
        _voices.assign(_hasMusicVoices ? count - kMusicVoices : count, Voice{});
        for (uint32_t index = 0; index < kMusicVoices; ++index) {
            _music[index].voice = static_cast<uint32_t>(_voices.size()) + index;
        }
        _musicScratch.assign(kMusicChunkFrames * kMaxAudioChannels, 0.0f);
    }

    void AudioManager::Shutdown() {
        for (MusicTrack& track : _music) {
            StopMusicTrack(track);
        }
        if (_backend) {
            _backend->Close();
            _backend.Reset();
        }
        _voices.clear();
    }

    AudioHandle AudioManager::Play(const AudioClip& clip, AudioBus bus, float volume) {
        if (!_backend || _voices.empty() || clip.GetState() != ResourceState::Loaded) {
            Record(clip.GetPath(), bus, volume, false);
            return {};
        }
        const uint32_t index = PickVoice();
        Voice& voice = _voices[index];
        voice.bus = bus;
        voice.volume = ClampVolume(volume);
        // A new generation first: the handle of the sound this voice played goes stale whether the start works or not
        ++voice.generation;
        if (!_backend->Start(index, clip, GetGain(bus, voice.volume))) {
            voice.startOrder = 0;
            Record(clip.GetPath(), bus, volume, false);
            return {};
        }
        ++_startedCount;
        voice.startOrder = _startedCount;
        Record(clip.GetPath(), bus, volume, true);
        return AudioHandle{index, voice.generation};
    }

    AudioHandle AudioManager::Play(PoolString clipPath, AudioBus bus, float volume) {
        if (clipPath.Empty()) {
            Record(clipPath, bus, volume, false);
            return {};
        }
        const ResourceHandle<AudioClip> clip = CoreManager::GetResourceManager().Load<AudioClip>(clipPath.ToStringView());
        if (!clip) {
            Record(clipPath, bus, volume, false);
            return {};
        }
        return Play(*clip, bus, volume);
    }

    void AudioManager::GetRecentPlays(Vector<AudioPlayRecord>& out) const {
        out.clear();
        const uint64_t count = eastl::min<uint64_t>(_requestCount, kAudioRecentPlays);
        for (uint64_t index = _requestCount - count + 1; index <= _requestCount; ++index) {
            out.push_back(_recent[(index - 1) % kAudioRecentPlays]);
        }
    }

    uint32_t AudioManager::GetPlayingCount() const {
        if (!_backend) {
            return 0;
        }
        uint32_t playing = 0;
        for (uint32_t index = 0; index < _voices.size(); ++index) {
            if (_backend->IsPlaying(index)) {
                ++playing;
            }
        }
        return playing;
    }

    void AudioManager::Record(PoolString clip, AudioBus bus, float volume, bool played) {
        ++_requestCount;
        AudioPlayRecord& record = _recent[(_requestCount - 1) % kAudioRecentPlays];
        record.index = _requestCount;
        record.clip = clip;
        record.bus = bus;
        record.volume = ClampVolume(volume);
        record.played = played;
    }

    void AudioManager::OnPlaySound(const AudioEvents::PlaySoundEvent& event) {
        Play(event.clip, event.bus, event.volume);
    }

    void AudioManager::OnStopAllSounds(const AudioEvents::StopAllSoundsEvent& /*event*/) {
        StopAll();
    }

    void AudioManager::OnPlayMusic(const AudioEvents::PlayMusicEvent& event) {
        PlayMusic(event.clip, event.volume, event.loop, event.fadeSeconds);
    }

    void AudioManager::OnStopMusic(const AudioEvents::StopMusicEvent& event) {
        StopMusic(event.fadeSeconds);
    }

    bool AudioManager::PlayMusic(PoolString clipPath, float volume, bool loop, float fadeSeconds) {
        MusicTrack& current = _music[_currentMusic];
        if (current.source && current.clip == clipPath && current.fadeStep >= 0.0f) {
            current.volume = ClampVolume(volume);
            current.loop = loop;
            _backend->SetGain(current.voice, GetMusicGain(current));
            Record(clipPath, AudioBus::Music, volume, true);
            return true;
        }
        if (!_backend || !_hasMusicVoices || clipPath.Empty()) {
            Record(clipPath, AudioBus::Music, volume, false);
            return false;
        }
        auto bytes = ReadAudioFile(clipPath);
        if (!bytes) {
            LOG_ERROR("Audio: the music: {}"_format(bytes.error()));
            Record(clipPath, AudioBus::Music, volume, false);
            return false;
        }
        auto source = OpenAudioStream(std::move(*bytes));
        if (!source) {
            LOG_ERROR("Audio: the music '{}': {}"_format(clipPath.ToStringView(), source.error()));
            Record(clipPath, AudioBus::Music, volume, false);
            return false;
        }

        const uint32_t next = (_currentMusic + 1) % kMusicVoices;
        MusicTrack& incoming = _music[next];
        StopMusicTrack(incoming);  // a track still fading out there is the oldest of three
        incoming.source = std::move(*source);
        incoming.clip = clipPath;
        incoming.volume = ClampVolume(volume);
        incoming.loop = loop;
        incoming.fade = fadeSeconds > 0.0f ? 0.0f : 1.0f;
        incoming.fadeStep = fadeSeconds > 0.0f ? 1.0f / fadeSeconds : 0.0f;
        incoming.framesSinceRewind = 0;
        incoming.ending = false;
        if (!_backend->StartStream(incoming.voice, incoming.source->GetFormat(), GetMusicGain(incoming))) {
            StopMusicTrack(incoming);
            Record(clipPath, AudioBus::Music, volume, false);
            return false;
        }
        FadeOutMusic(current, fadeSeconds);
        _currentMusic = next;
        FeedMusic(incoming);  // the first buffer now: a stream that starts empty would wait a frame
        Record(clipPath, AudioBus::Music, volume, true);
        return true;
    }

    void AudioManager::StopMusic(float fadeSeconds) {
        for (MusicTrack& track : _music) {
            FadeOutMusic(track, fadeSeconds);
        }
    }

    AudioMusicState AudioManager::GetMusicState() const {
        const MusicTrack& current = _music[_currentMusic];
        if (!current.source) {
            return {};
        }
        return AudioMusicState{current.clip, true, current.loop, current.volume, current.fadeStep != 0.0f};
    }

    void AudioManager::Update(float deltaSeconds) {
        if (!_backend) {
            return;
        }
        for (MusicTrack& track : _music) {
            if (!track.source) {
                continue;
            }
            if (track.fadeStep != 0.0f && deltaSeconds > 0.0f) {
                track.fade += track.fadeStep * deltaSeconds;
                if (track.fade <= 0.0f) {
                    StopMusicTrack(track);
                    continue;
                }
                if (track.fade >= 1.0f) {
                    track.fade = 1.0f;
                    track.fadeStep = 0.0f;
                }
                _backend->SetGain(track.voice, GetMusicGain(track));
            }
            FeedMusic(track);
            if (track.ending && !_backend->IsPlaying(track.voice)) {
                StopMusicTrack(track);
            }
        }
    }

    float AudioManager::GetMusicGain(const MusicTrack& track) const {
        return GetGain(AudioBus::Music, track.volume) * track.fade;
    }

    void AudioManager::FeedMusic(MusicTrack& track) {
        if (track.ending) {
            return;
        }
        const AudioFormat format = track.source->GetFormat();
        const auto target = static_cast<uint64_t>(static_cast<float>(format.sampleRate) * kMusicBufferSeconds);
        const Span<float> chunk(_musicScratch.data(), kMusicChunkFrames * format.channels);
        for (uint32_t read = 0; read < kMaxMusicChunksPerUpdate && _backend->GetQueuedFrames(track.voice) < target; ++read) {
            const size_t frames = track.source->Read(chunk);
            if (frames == 0) {
                // A track that gave nothing since its start would rewind for ever: it ends instead
                if (track.loop && track.framesSinceRewind > 0 && track.source->Rewind()) {
                    track.framesSinceRewind = 0;
                    continue;
                }
                _backend->EndStream(track.voice);
                track.ending = true;
                return;
            }
            track.framesSinceRewind += frames;
            _backend->QueueStream(track.voice, chunk.first(frames * format.channels));
        }
    }

    void AudioManager::FadeOutMusic(MusicTrack& track, float fadeSeconds) {
        if (!track.source) {
            return;
        }
        if (fadeSeconds <= 0.0f) {
            StopMusicTrack(track);
            return;
        }
        // From the level it has now, so a track that was still fading in does not jump up first
        track.fadeStep = -1.0f / fadeSeconds;
    }

    void AudioManager::StopMusicTrack(MusicTrack& track) {
        if (track.source && _backend) {
            _backend->Stop(track.voice);
        }
        track.source.reset();
        track.clip = PoolString();
        track.fadeStep = 0.0f;
        track.ending = false;
    }

    void AudioManager::Stop(AudioHandle handle) {
        if (IsCurrent(handle)) {
            _backend->Stop(handle.voice);
            ++_voices[handle.voice].generation;
        }
    }

    void AudioManager::StopAll() {
        if (!_backend) {
            return;
        }
        for (uint32_t index = 0; index < _voices.size(); ++index) {
            _backend->Stop(index);
            ++_voices[index].generation;
        }
        StopMusic();
    }

    bool AudioManager::IsPlaying(AudioHandle handle) const {
        return IsCurrent(handle) && _backend->IsPlaying(handle.voice);
    }

    void AudioManager::ApplySettings(const AudioSettings& settings) {
        _settings._master = settings._master;
        _settings._sfx = settings._sfx;
        _settings._music = settings._music;
        _settings._ui = settings._ui;
        ClampSettings(_settings);
        RefreshGains();
    }

    void AudioManager::SetMasterVolume(float volume) {
        _settings._master._volume = ClampVolume(volume);
        RefreshGains();
    }

    void AudioManager::SetMasterMuted(bool muted) {
        _settings._master._muted = muted;
        RefreshGains();
    }

    void AudioManager::SetBusVolume(AudioBus bus, float volume) {
        _settings.LevelOf(bus)._volume = ClampVolume(volume);
        RefreshGains();
    }

    void AudioManager::SetBusMuted(AudioBus bus, bool muted) {
        _settings.LevelOf(bus)._muted = muted;
        RefreshGains();
    }

    float AudioManager::GetGain(AudioBus bus, float volume) const {
        const AudioLevel& level = _settings.LevelOf(bus);
        if (_settings._master._muted || level._muted) {
            return 0.0f;
        }
        return ClampVolume(volume) * level._volume * _settings._master._volume;
    }

    bool AudioManager::IsCurrent(AudioHandle handle) const {
        return _backend && handle.voice < _voices.size() && _voices[handle.voice].generation == handle.generation && _voices[handle.voice].startOrder != 0;
    }

    uint32_t AudioManager::PickVoice() const {
        uint32_t oldest = 0;
        for (uint32_t index = 0; index < _voices.size(); ++index) {
            if (!_backend->IsPlaying(index)) {
                return index;
            }
            if (_voices[index].startOrder < _voices[oldest].startOrder) {
                oldest = index;
            }
        }
        return oldest;
    }

    void AudioManager::RefreshGains() {
        // Every Set*() and ApplySettings() ends here
        ++_levelsRevision;
        if (!_backend) {
            return;
        }
        for (uint32_t index = 0; index < _voices.size(); ++index) {
            const Voice& voice = _voices[index];
            if (voice.startOrder != 0) {
                _backend->SetGain(index, GetGain(voice.bus, voice.volume));
            }
        }
        for (const MusicTrack& track : _music) {
            if (track.source) {
                _backend->SetGain(track.voice, GetMusicGain(track));
            }
        }
    }

}  // namespace BECore
