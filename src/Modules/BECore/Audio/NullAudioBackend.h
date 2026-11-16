#pragma once

#include <BECore/Audio/IAudioBackend.h>

namespace BECore {

    /// A backend that plays nothing: a sound ends the moment it starts, a stream does not start. Used where there is no output (a machine without a device, a build without one)
    class NullAudioBackend : public IAudioBackend {
        BE_CLASS(NullAudioBackend)
    public:
        bool Open(uint32_t voiceCount) override {
            _voiceCount = voiceCount;
            return true;
        }

        void Close() override {
            _voiceCount = 0;
        }

        [[nodiscard]] uint32_t GetVoiceCount() const override {
            return _voiceCount;
        }

        bool Start(uint32_t voice, const AudioClip& /*clip*/, float /*gain*/) override {
            return voice < _voiceCount;
        }

        void Stop(uint32_t /*voice*/) override {}

        void SetGain(uint32_t /*voice*/, float /*gain*/) override {}

        [[nodiscard]] bool IsPlaying(uint32_t /*voice*/) const override {
            return false;
        }

        /// No stream: nothing would drain it, so the music would be decoded for nobody
        bool StartStream(uint32_t /*voice*/, AudioFormat /*format*/, float /*gain*/) override {
            return false;
        }

        bool QueueStream(uint32_t /*voice*/, Span<const float> /*samples*/) override {
            return false;
        }

        void EndStream(uint32_t /*voice*/) override {}

        [[nodiscard]] uint64_t GetQueuedFrames(uint32_t /*voice*/) const override {
            return 0;
        }

    private:
        uint32_t _voiceCount = 0;
    };

}  // namespace BECore
