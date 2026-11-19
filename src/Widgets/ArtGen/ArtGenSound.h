#pragma once

#include <BECore/Audio/AudioDecode.h>
#include <BECore/Control/Json.h>
#include <BECore/Types/Types.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /**
     * @brief What Accept does to a generated sound before it becomes a game clip: a raw one has silence at the start, a long tail, any
     * loudness, and stereo where a game wants mono. Applied in the order of the fields.
     */
    struct ArtGenSoundSettings {
        static constexpr float kDefaultTrimDb = -50.0f;
        static constexpr float kDefaultFadeInMs = 5.0f;
        static constexpr float kDefaultFadeOutMs = 30.0f;
        static constexpr float kDefaultPeakDb = -1.0f;

        bool mono = true;                         // the channels averaged into one
        Optional<float> trimDb = kDefaultTrimDb;  // silence quieter than this (dBFS) cut from both ends; none: no trim
        float maxSeconds = 0.0f;                  // cut after this many seconds (the fade out is then the tail); 0: no limit
        uint32_t sampleRate = 0;                  // resampled to this rate; 0: the file's own
        float fadeInMs = kDefaultFadeInMs;
        float fadeOutMs = kDefaultFadeOutMs;
        Optional<float> peakDb = kDefaultPeakDb;  // the loudest sample brought to this (dBFS); none: the level as generated

        [[nodiscard]] bool operator==(const ArtGenSoundSettings&) const = default;
    };

    /// The settings as `artgen_accept` takes them and the provenance records them: `{mono, trimDb|null, maxSeconds, sampleRate, fadeInMs,
    /// fadeOutMs, peakDb|null}`
    [[nodiscard]] Json ArtGenSoundSettingsToJson(const ArtGenSoundSettings& settings);
    /// Reads what ArtGenSoundSettingsToJson writes; a missing key keeps the default, a wrong type or a value out of range is the error
    [[nodiscard]] std::expected<ArtGenSoundSettings, String> ArtGenSoundSettingsFromJson(const Json& json);

    /// Averages the channels (`mono`), trims the silence at both ends, cuts at `maxSeconds`, resamples linearly, fades in and out (the last
    /// frame is silent) and brings the peak to `peakDb`, in that order; refused when nothing is left (all of it under the trim threshold)
    /// or a setting is out of range. Pure
    [[nodiscard]] AudioDecodeResult ProcessArtGenSound(const DecodedAudio& audio, const ArtGenSoundSettings& settings);

    /// The length and the loudest sample of a sound, for the panel's row
    struct ArtGenSoundStats {
        double seconds = 0.0;
        float peak = 0.0f;  // 0..1 (and above for a float file that clips)

        /// The peak in dBFS; the floor of the panel for silence
        [[nodiscard]] float PeakDb() const;
    };

    [[nodiscard]] ArtGenSoundStats MeasureArtGenSound(const DecodedAudio& audio);

    /// The waveform of a thumbnail: @p buckets pairs of (lowest, highest) sample, all channels together; empty for an empty sound
    [[nodiscard]] Vector<float> BuildArtGenWaveform(const DecodedAudio& audio, size_t buckets);

}  // namespace BECore
