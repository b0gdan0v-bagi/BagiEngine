#include "ArtGenSound.h"

#include <algorithm>
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

        std::expected<Optional<float>, String> OptionalNumber(const Json& json, StringView key, Optional<float> fallback) {
            const Json* value = FindJsonMember(json, key);
            if (value == nullptr) {
                return fallback;
            }
            if (value->is_null()) {
                return Optional<float>();
            }
            if (!value->is_number()) {
                return std::unexpected(Format("\"{}\" must be a number or null", key));
            }
            return Optional<float>(value->get<float>());
        }

        std::expected<float, String> Number(const Json& json, StringView key, float fallback) {
            const Json* value = FindJsonMember(json, key);
            if (value == nullptr) {
                return fallback;
            }
            if (!value->is_number()) {
                return std::unexpected(Format("\"{}\" must be a number", key));
            }
            return value->get<float>();
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

    Json ArtGenSoundSettingsToJson(const ArtGenSoundSettings& settings) {
        Json json = Json::object();
        json["mono"] = settings.mono;
        json["trimDb"] = settings.trimDb ? Json(*settings.trimDb) : Json();
        json["maxSeconds"] = settings.maxSeconds;
        json["sampleRate"] = settings.sampleRate;
        json["fadeInMs"] = settings.fadeInMs;
        json["fadeOutMs"] = settings.fadeOutMs;
        json["peakDb"] = settings.peakDb ? Json(*settings.peakDb) : Json();
        return json;
    }

    std::expected<ArtGenSoundSettings, String> ArtGenSoundSettingsFromJson(const Json& json) {
        ArtGenSoundSettings settings;
        if (json.is_null()) {
            return settings;
        }
        if (!json.is_object()) {
            return std::unexpected(String("the sound settings must be an object"));
        }
        if (const Json* mono = FindJsonMember(json, "mono"); mono != nullptr) {
            if (!mono->is_boolean()) {
                return std::unexpected(String("\"mono\" must be a boolean"));
            }
            settings.mono = mono->get<bool>();
        }
        auto trim = OptionalNumber(json, "trimDb", settings.trimDb);
        auto peak = OptionalNumber(json, "peakDb", settings.peakDb);
        auto maxSeconds = Number(json, "maxSeconds", settings.maxSeconds);
        auto fadeIn = Number(json, "fadeInMs", settings.fadeInMs);
        auto fadeOut = Number(json, "fadeOutMs", settings.fadeOutMs);
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
        if (const Json* rate = FindJsonMember(json, "sampleRate"); rate != nullptr) {
            if (!rate->is_number_unsigned() && !(rate->is_number_integer() && rate->get<int64_t>() >= 0)) {
                return std::unexpected(String("\"sampleRate\" must be a whole number"));
            }
            settings.sampleRate = static_cast<uint32_t>(std::min<uint64_t>(rate->get<uint64_t>(), UINT32_MAX));
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
