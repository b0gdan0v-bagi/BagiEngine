#pragma once

#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/IAudioBackend.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore::Tests {

    /// Records what AudioManager asks of a backend; a voice plays until the test ends it, a stream until the test drains it after its end
    class FakeAudioBackend : public IAudioBackend {
    public:
        bool opens = true;
        bool queues = true;  ///< false: QueueStream() refuses, as a closed output does
        Vector<bool> playing;
        Vector<float> gains;
        Vector<uint32_t> starts;        ///< the voice of every Start(), in order
        Vector<uint32_t> streamStarts;  ///< the voice of every StartStream(), in order
        Vector<AudioFormat> streamFormats;
        Vector<uint64_t> queuedFrames;  ///< per voice, what QueueStream() put and Drain() did not take
        Vector<uint64_t> totalFrames;   ///< per voice, every frame queued since its StartStream()
        Vector<bool> ended;             ///< per voice, EndStream() since its StartStream()
        uint32_t closes = 0;

        /// The device took @p frames of the stream of @p voice; a stream that ended and has nothing left stops
        void Drain(uint32_t voice, uint64_t frames) {
            queuedFrames[voice] -= eastl::min(queuedFrames[voice], frames);
            if (ended[voice] && queuedFrames[voice] == 0) {
                playing[voice] = false;
            }
        }

        bool Open(uint32_t voiceCount) override {
            if (!opens) {
                return false;
            }
            playing.assign(voiceCount, false);
            gains.assign(voiceCount, -1.0f);
            streamFormats.assign(voiceCount, AudioFormat{});
            queuedFrames.assign(voiceCount, 0);
            totalFrames.assign(voiceCount, 0);
            ended.assign(voiceCount, false);
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
            queuedFrames[voice] = 0;
        }

        void SetGain(uint32_t voice, float gain) override {
            gains[voice] = gain;
        }

        [[nodiscard]] bool IsPlaying(uint32_t voice) const override {
            return voice < playing.size() && playing[voice];
        }

        bool StartStream(uint32_t voice, AudioFormat format, float gain) override {
            streamStarts.push_back(voice);
            playing[voice] = true;
            gains[voice] = gain;
            streamFormats[voice] = format;
            queuedFrames[voice] = 0;
            totalFrames[voice] = 0;
            ended[voice] = false;
            return true;
        }

        bool QueueStream(uint32_t voice, Span<const float> samples) override {
            if (!queues) {
                return false;
            }
            const uint64_t frames = samples.size() / streamFormats[voice].channels;
            queuedFrames[voice] += frames;
            totalFrames[voice] += frames;
            return true;
        }

        void EndStream(uint32_t voice) override {
            ended[voice] = true;
        }

        [[nodiscard]] uint64_t GetQueuedFrames(uint32_t voice) const override {
            return voice < queuedFrames.size() ? queuedFrames[voice] : 0;
        }
    };

}  // namespace BECore::Tests
