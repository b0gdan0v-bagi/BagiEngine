#pragma once

#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/IAudioBackend.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore::Tests {

    /// Records what AudioManager asks of a backend; a voice plays until the test ends it
    class FakeAudioBackend : public IAudioBackend {
    public:
        bool opens = true;
        Vector<bool> playing;
        Vector<float> gains;
        Vector<uint32_t> starts;  ///< the voice of every Start(), in order
        uint32_t closes = 0;

        bool Open(uint32_t voiceCount) override {
            if (!opens) {
                return false;
            }
            playing.assign(voiceCount, false);
            gains.assign(voiceCount, -1.0f);
            return true;
        }

        void Close() override {
            ++closes;
            playing.clear();
            gains.clear();
        }

        [[nodiscard]] uint32_t GetVoiceCount() const override {
            return static_cast<uint32_t>(playing.size());
        }

        bool Start(uint32_t voice, const AudioClip& /*clip*/, float gain) override {
            starts.push_back(voice);
            playing[voice] = true;
            gains[voice] = gain;
            return true;
        }

        void Stop(uint32_t voice) override {
            playing[voice] = false;
        }

        void SetGain(uint32_t voice, float gain) override {
            gains[voice] = gain;
        }

        [[nodiscard]] bool IsPlaying(uint32_t voice) const override {
            return voice < playing.size() && playing[voice];
        }
    };

}  // namespace BECore::Tests
