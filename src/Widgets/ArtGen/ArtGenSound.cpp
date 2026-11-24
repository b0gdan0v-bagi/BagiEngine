#include "ArtGenSound.h"

#include <BECore/Control/ControlParams.h>
#include <cmath>

namespace BECore {

    namespace {

        constexpr float kDecibelsPerTwentyLog = 20.0f;
        constexpr float kMinDb = -120.0f;
        constexpr float kMinPeakDb = -60.0f;
        constexpr float kMaxFadeMs = 10000.0f;
        constexpr float kMillisecondsPerSecond = 1000.0f;
        constexpr float kSilenceDb = -120.0f;

        float FromDb(float db) {
            return std::pow(10.0f, db / kDecibelsPerTwentyLog);
        }

        size_t FramesOf(float milliseconds, uint32_t rate) {
            return static_cast<size_t>(std::lround(static_cast<double>(milliseconds) * rate / kMillisecondsPerSecond));
        }

        DecodedAudio Mono(const DecodedAudio& audio) {
            DecodedAudio mono;
            mono.format = AudioFormat{audio.format.sampleRate, 1};
            const size_t channels = audio.format.channels;
            const size_t frames = audio.GetFrameCount();
            mono.samples.resize(frames);
            for (size_t frame = 0; frame < frames; ++frame) {
                float sum = 0.0f;
                for (size_t channel = 0; channel < channels; ++channel) {
                    sum += audio.samples[frame * channels + channel];
                }
                mono.samples[frame] = sum / static_cast<float>(channels);
            }
            return mono;
        }

        /// The frame range whose loudest channel is above @p threshold, as [first, end); empty when none is
        Pair<size_t, size_t> LoudRange(const DecodedAudio& audio, float threshold) {
            const size_t channels = audio.format.channels;
            const size_t frames = audio.GetFrameCount();
            auto loud = [&](size_t frame) {
                for (size_t channel = 0; channel < channels; ++channel) {
                    if (std::abs(audio.samples[frame * channels + channel]) > threshold) {
                        return true;
                    }
                }
                return false;
            };
            size_t first = 0;
            while (first < frames && !loud(first)) {
                ++first;
            }
            size_t end = frames;
            while (end > first && !loud(end - 1)) {
                --end;
            }
            return {first, end};
        }

        void KeepFrames(DecodedAudio& audio, size_t first, size_t end) {
            const size_t channels = audio.format.channels;
            audio.samples.erase(audio.samples.begin() + static_cast<ptrdiff_t>(end * channels), audio.samples.end());
            audio.samples.erase(audio.samples.begin(), audio.samples.begin() + static_cast<ptrdiff_t>(first * channels));
        }

        /// Linear interpolation between neighbouring frames: enough for the short effects a game keeps; no filter against aliasing
        DecodedAudio Resample(const DecodedAudio& audio, uint32_t rate) {
            const size_t channels = audio.format.channels;
            const size_t frames = audio.GetFrameCount();
            const double step = static_cast<double>(audio.format.sampleRate) / rate;
            const auto outFrames = std::max<size_t>(1, static_cast<size_t>(std::llround(static_cast<double>(frames) / step)));
            DecodedAudio out;
            out.format = AudioFormat{rate, audio.format.channels};
            out.samples.resize(outFrames * channels);
            for (size_t frame = 0; frame < outFrames; ++frame) {
                const double position = static_cast<double>(frame) * step;
                const size_t left = std::min(static_cast<size_t>(position), frames - 1);
                const size_t right = std::min(left + 1, frames - 1);
                const auto fraction = static_cast<float>(position - static_cast<double>(left));
                for (size_t channel = 0; channel < channels; ++channel) {
                    const float a = audio.samples[left * channels + channel];
                    const float b = audio.samples[right * channels + channel];
                    out.samples[frame * channels + channel] = a + (b - a) * fraction;
                }
            }
            return out;
        }

        void Fade(DecodedAudio& audio, size_t inFrames, size_t outFrames) {
            const size_t channels = audio.format.channels;
            const size_t frames = audio.GetFrameCount();
            inFrames = std::min(inFrames, frames);
            outFrames = std::min(outFrames, frames);
            for (size_t frame = 0; frame < inFrames; ++frame) {
                const float gain = static_cast<float>(frame) / static_cast<float>(inFrames);
                for (size_t channel = 0; channel < channels; ++channel) {
                    audio.samples[frame * channels + channel] *= gain;
                }
            }
            for (size_t i = 0; i < outFrames; ++i) {
                // The last frame is silent, so the clip ends without a click
                const size_t frame = frames - 1 - i;
                const float gain = static_cast<float>(i) / static_cast<float>(outFrames);
                for (size_t channel = 0; channel < channels; ++channel) {
                    audio.samples[frame * channels + channel] *= gain;
                }
            }
        }

        float Peak(const Vector<float>& samples) {
            float peak = 0.0f;
            for (const float sample : samples) {
                peak = std::max(peak, std::abs(sample));
            }
            return peak;
        }

        bool IsFiniteIn(float value, float low, float high) {
            return std::isfinite(value) && value >= low && value <= high;
        }

        /// @p value as a float; the error says what @p key must be. `1e999` is a valid number to the tape (the DOM refused the document), so a number
        /// that is not finite is refused here
        std::expected<float, String> FiniteFloat(JsonView value, StringView key, StringView expected) {
            const Optional<double> number = value.AsDouble();
            if (!number) {
                return std::unexpected(Format("\"{}\" must be {}", key, expected));
            }
            if (!std::isfinite(*number)) {
                return std::unexpected(Format("\"{}\" must be a finite number", key));
            }
            return static_cast<float>(*number);
        }

        std::expected<Optional<float>, String> OptionalNumber(JsonView object, StringView key, Optional<float> fallback) {
            const Optional<JsonView> value = FindParam(object, key);
            if (!value) {
                return fallback;
            }
            if (value->IsNull()) {
                return Optional<float>();
            }
            const auto number = FiniteFloat(*value, key, "a number or null");
            if (!number) {
                return std::unexpected(number.error());
            }
            return Optional<float>(*number);
        }

        std::expected<float, String> Number(JsonView object, StringView key, float fallback) {
            const Optional<JsonView> value = FindParam(object, key);
            if (!value) {
                return fallback;
            }
            return FiniteFloat(*value, key, "a number");
        }

        std::expected<void, String> Check(const ArtGenSoundSettings& settings) {
            if (settings.trimDb && !IsFiniteIn(*settings.trimDb, kMinDb, 0.0f)) {
                return std::unexpected(Format("trimDb must be {} to 0", kMinDb));
            }
            if (!std::isfinite(settings.maxSeconds) || settings.maxSeconds < 0.0f) {
                return std::unexpected(String("maxSeconds must be 0 (no limit) or more"));
            }
            if (settings.sampleRate != 0 && (settings.sampleRate < kMinAudioSampleRate || settings.sampleRate > kMaxAudioSampleRate)) {
                return std::unexpected(Format("sampleRate must be 0 (the file's) or {} to {}", kMinAudioSampleRate, kMaxAudioSampleRate));
            }
            if (!IsFiniteIn(settings.fadeInMs, 0.0f, kMaxFadeMs) || !IsFiniteIn(settings.fadeOutMs, 0.0f, kMaxFadeMs)) {
                return std::unexpected(Format("the fades must be 0 to {} ms", kMaxFadeMs));
            }
            if (settings.peakDb && !IsFiniteIn(*settings.peakDb, kMinPeakDb, 0.0f)) {
                return std::unexpected(Format("peakDb must be {} to 0", kMinPeakDb));
            }
            return {};
        }

    }  // namespace

    void WriteArtGenSoundSettings(JsonWriter& writer, const ArtGenSoundSettings& settings) {
        const auto optionalDouble = [&writer](StringView key, const Optional<float>& value) {
            writer.Key(key);
            if (value) {
                writer.Value(static_cast<double>(*value));
            } else {
                writer.Null();
            }
        };
        writer.BeginObject();
        writer.Member("fadeInMs", static_cast<double>(settings.fadeInMs));
        writer.Member("fadeOutMs", static_cast<double>(settings.fadeOutMs));
        writer.Member("maxSeconds", static_cast<double>(settings.maxSeconds));
        writer.Member("mono", settings.mono);
        optionalDouble("peakDb", settings.peakDb);
        writer.Member("sampleRate", settings.sampleRate);
        optionalDouble("trimDb", settings.trimDb);
        writer.EndObject();
    }

    std::expected<ArtGenSoundSettings, String> ReadArtGenSoundSettings(JsonView value) {
        ArtGenSoundSettings settings;
        if (!value.IsValid() || value.IsNull()) {
            return settings;
        }
        if (!value.IsObject()) {
            return std::unexpected(String("the sound settings must be an object"));
        }
        if (const Optional<JsonView> mono = FindParam(value, "mono")) {
            const Optional<bool> flag = mono->AsBool();
            if (!flag) {
                return std::unexpected(String("\"mono\" must be a boolean"));
            }
            settings.mono = *flag;
        }
        auto trim = OptionalNumber(value, "trimDb", settings.trimDb);
        auto peak = OptionalNumber(value, "peakDb", settings.peakDb);
        auto maxSeconds = Number(value, "maxSeconds", settings.maxSeconds);
        auto fadeIn = Number(value, "fadeInMs", settings.fadeInMs);
        auto fadeOut = Number(value, "fadeOutMs", settings.fadeOutMs);
        for (const String* error :
             {trim ? nullptr : &trim.error(), peak ? nullptr : &peak.error(), maxSeconds ? nullptr : &maxSeconds.error(), fadeIn ? nullptr : &fadeIn.error(), fadeOut ? nullptr : &fadeOut.error()}) {
            if (error != nullptr) {
                return std::unexpected(*error);
            }
        }
        settings.trimDb = *trim;
        settings.peakDb = *peak;
        settings.maxSeconds = *maxSeconds;
        settings.fadeInMs = *fadeIn;
        settings.fadeOutMs = *fadeOut;
        if (const Optional<JsonView> rate = FindParam(value, "sampleRate")) {
            const Optional<uint64_t> whole = rate->AsUint64();
            if (!whole) {
                return std::unexpected(String("\"sampleRate\" must be a whole number"));
            }
            settings.sampleRate = static_cast<uint32_t>(std::min<uint64_t>(*whole, UINT32_MAX));
        }
        if (auto checked = Check(settings); !checked) {
            return std::unexpected(checked.error());
        }
        return settings;
    }

    AudioDecodeResult ProcessArtGenSound(const DecodedAudio& audio, const ArtGenSoundSettings& settings) {
        if (auto checked = Check(settings); !checked) {
            return std::unexpected(checked.error());
        }
        if (audio.format.channels == 0 || audio.GetFrameCount() == 0) {
            return std::unexpected(String("the sound is empty"));
        }
        DecodedAudio sound = settings.mono && audio.format.channels > 1 ? Mono(audio) : audio;
        if (settings.trimDb) {
            const auto [first, end] = LoudRange(sound, FromDb(*settings.trimDb));
            if (first >= end) {
                return std::unexpected(Format("the whole sound is quieter than {} dBFS: nothing is left after the trim", *settings.trimDb));
            }
            KeepFrames(sound, first, end);
        }
        if (settings.maxSeconds > 0.0f) {
            const size_t limit = std::max<size_t>(1, FramesOf(settings.maxSeconds * kMillisecondsPerSecond, sound.format.sampleRate));
            if (sound.GetFrameCount() > limit) {
                KeepFrames(sound, 0, limit);
            }
        }
        if (settings.sampleRate != 0 && settings.sampleRate != sound.format.sampleRate) {
            sound = Resample(sound, settings.sampleRate);
        }
        Fade(sound, FramesOf(settings.fadeInMs, sound.format.sampleRate), FramesOf(settings.fadeOutMs, sound.format.sampleRate));
        if (settings.peakDb) {
            const float peak = Peak(sound.samples);
            if (peak > 0.0f) {
                const float gain = FromDb(*settings.peakDb) / peak;
                for (float& sample : sound.samples) {
                    sample *= gain;
                }
            }
        }
        return sound;
    }

    float ArtGenSoundStats::PeakDb() const {
        return peak > 0.0f ? std::max(kSilenceDb, kDecibelsPerTwentyLog * std::log10(peak)) : kSilenceDb;
    }

    ArtGenSoundStats MeasureArtGenSound(const DecodedAudio& audio) {
        ArtGenSoundStats stats;
        stats.seconds = audio.format.sampleRate == 0 ? 0.0 : static_cast<double>(audio.GetFrameCount()) / audio.format.sampleRate;
        stats.peak = Peak(audio.samples);
        return stats;
    }

    Vector<float> BuildArtGenWaveform(const DecodedAudio& audio, size_t buckets) {
        const size_t frames = audio.GetFrameCount();
        const size_t channels = audio.format.channels;
        buckets = std::min(buckets, frames);
        Vector<float> waveform;
        if (buckets == 0) {
            return waveform;
        }
        waveform.reserve(buckets * 2);
        for (size_t bucket = 0; bucket < buckets; ++bucket) {
            const size_t first = bucket * frames / buckets;
            const size_t end = (bucket + 1) * frames / buckets;
            float low = 0.0f;
            float high = 0.0f;
            for (size_t i = first * channels; i < end * channels; ++i) {
                low = std::min(low, audio.samples[i]);
                high = std::max(high, audio.samples[i]);
            }
            waveform.push_back(low);
            waveform.push_back(high);
        }
        return waveform;
    }

}  // namespace BECore
