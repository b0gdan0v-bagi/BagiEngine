#include "SDLAudioBackend.h"

#include <BECore/Audio/AudioClip.h>
#include <Events/ApplicationEvents.h>
#include <Generated/SDLAudioBackend.gen.hpp>
#include <SDL3/SDL.h>
#include <climits>

namespace BECore {

    namespace {

        constexpr const char* kHeadlessAudioDriver = "dummy";

    }  // namespace

    SDLAudioBackend::~SDLAudioBackend() {
        Close();
    }

    bool SDLAudioBackend::Open(uint32_t voiceCount) {
        Close();
        // The driver is read when the audio subsystem starts, so the hint has to come first
        if (_headless) {
            SDL_SetHint(SDL_HINT_AUDIO_DRIVER, kHeadlessAudioDriver);
        }
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            LOG_WARNING("SDLAudioBackend: no audio subsystem: {}"_format(SDL_GetError()));
            Close();
            return false;
        }
        _subsystem = true;
        _device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
        if (_device == 0) {
            LOG_WARNING("SDLAudioBackend: no playback device: {}"_format(SDL_GetError()));
            Close();
            return false;
        }
        SDL_AudioSpec deviceSpec{};
        if (!SDL_GetAudioDeviceFormat(_device, &deviceSpec, nullptr)) {
            LOG_WARNING("SDLAudioBackend: the playback device has no format: {}"_format(SDL_GetError()));
            Close();
            return false;
        }
        _streams.reserve(voiceCount);
        for (uint32_t index = 0; index < voiceCount; ++index) {
            SDL_AudioStream* stream = SDL_CreateAudioStream(&deviceSpec, &deviceSpec);
            if (stream == nullptr) {
                LOG_WARNING("SDLAudioBackend: cannot make voice {}: {}"_format(index, SDL_GetError()));
                Close();
                return false;
            }
            _streams.push_back(stream);
        }
        _streamChannels.assign(voiceCount, 0);
        if (!SDL_BindAudioStreams(_device, _streams.data(), static_cast<int>(_streams.size()))) {
            LOG_WARNING("SDLAudioBackend: cannot bind the voices: {}"_format(SDL_GetError()));
            Close();
            return false;
        }
        if (!_subscribed) {
            Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &SDLAudioBackend::OnCleanUp>(this);
            _subscribed = true;
        }
        return true;
    }

    void SDLAudioBackend::Close() {
        for (SDL_AudioStream* stream : _streams) {
            SDL_DestroyAudioStream(stream);  // unbinds it too
        }
        _streams.clear();
        _streamChannels.clear();
        if (_device != 0) {
            SDL_CloseAudioDevice(_device);
            _device = 0;
        }
        if (_subsystem) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            _subsystem = false;
        }
        if (_headless) {
            SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);  // the next backend picks its driver again
        }
    }

    uint32_t SDLAudioBackend::GetVoiceCount() const {
        return static_cast<uint32_t>(_streams.size());
    }

    bool SDLAudioBackend::Start(uint32_t voice, const AudioClip& clip, float gain) {
        if (voice >= _streams.size() || clip.GetSamples().empty()) {
            return false;
        }
        // SDL takes the byte count as an int: a clip of 2 GiB and more (about three hours of stereo at 48 kHz) cannot be put in one call
        if (clip.GetSamples().size_bytes() > static_cast<size_t>(INT_MAX)) {
            LOG_WARNING("SDLAudioBackend: '{}' is too long to play as one clip"_format(clip.GetPath().ToStringView()));
            return false;
        }
        _streamChannels[voice] = 0;
        SDL_AudioStream* stream = _streams[voice];
        const AudioFormat& format = clip.GetFormat();
        const SDL_AudioSpec source{SDL_AUDIO_F32, format.channels, static_cast<int>(format.sampleRate)};
        const Span<const float> samples = clip.GetSamples();
        const bool started = SDL_ClearAudioStream(stream) && SDL_SetAudioStreamFormat(stream, &source, nullptr) && SDL_SetAudioStreamGain(stream, gain) &&
                             SDL_PutAudioStreamData(stream, samples.data(), static_cast<int>(samples.size_bytes())) && SDL_FlushAudioStream(stream);
        if (!started) {
            LOG_WARNING("SDLAudioBackend: cannot start '{}' on voice {}: {}"_format(clip.GetPath().ToStringView(), voice, SDL_GetError()));
            SDL_ClearAudioStream(stream);
        }
        return started;
    }

    void SDLAudioBackend::Stop(uint32_t voice) {
        if (voice < _streams.size()) {
            SDL_ClearAudioStream(_streams[voice]);
        }
    }

    void SDLAudioBackend::SetGain(uint32_t voice, float gain) {
        if (voice < _streams.size()) {
            SDL_SetAudioStreamGain(_streams[voice], gain);
        }
    }

    bool SDLAudioBackend::IsPlaying(uint32_t voice) const {
        if (voice >= _streams.size()) {
            return false;
        }
        // Queued: put and not converted yet; available: converted and not taken by the device yet
        return SDL_GetAudioStreamQueued(_streams[voice]) > 0 || SDL_GetAudioStreamAvailable(_streams[voice]) > 0;
    }

    bool SDLAudioBackend::StartStream(uint32_t voice, AudioFormat format, float gain) {
        if (voice >= _streams.size() || format.channels == 0) {
            return false;
        }
        SDL_AudioStream* stream = _streams[voice];
        const SDL_AudioSpec source{SDL_AUDIO_F32, format.channels, static_cast<int>(format.sampleRate)};
        if (!(SDL_ClearAudioStream(stream) && SDL_SetAudioStreamFormat(stream, &source, nullptr) && SDL_SetAudioStreamGain(stream, gain))) {
            LOG_WARNING("SDLAudioBackend: cannot start a stream on voice {}: {}"_format(voice, SDL_GetError()));
            _streamChannels[voice] = 0;
            return false;
        }
        _streamChannels[voice] = format.channels;
        return true;
    }

    bool SDLAudioBackend::QueueStream(uint32_t voice, Span<const float> samples) {
        if (voice >= _streams.size() || _streamChannels[voice] == 0 || samples.size_bytes() > static_cast<size_t>(INT_MAX)) {
            return false;
        }
        return SDL_PutAudioStreamData(_streams[voice], samples.data(), static_cast<int>(samples.size_bytes()));
    }

    void SDLAudioBackend::EndStream(uint32_t voice) {
        if (voice < _streams.size()) {
            // The resampler keeps a few frames back until it knows no more come
            SDL_FlushAudioStream(_streams[voice]);
        }
    }

    uint64_t SDLAudioBackend::GetQueuedFrames(uint32_t voice) const {
        if (voice >= _streams.size() || _streamChannels[voice] == 0) {
            return 0;
        }
        // Queued is the input not converted yet, in the stream's input format; what is converted and waits for the device is a few ms more
        const int bytes = SDL_GetAudioStreamQueued(_streams[voice]);
        return bytes > 0 ? static_cast<uint64_t>(bytes) / (sizeof(float) * _streamChannels[voice]) : 0;
    }

    void SDLAudioBackend::OnCleanUp() {
        // Only closed: AudioManager still holds the backend (dropping it here would destroy the handler that runs), and a closed backend plays nothing
        Close();
    }

}  // namespace BECore
