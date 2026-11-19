#include <BECore/Audio/AudioDecode.h>
#include <Fuzz/FuzzChecks.h>
#include <Fuzz/FuzzTargets.h>
#include <cmath>

namespace BECore::Fuzz {

    void FuzzAudioWav(const uint8_t* data, size_t size) {
        const Span<const uint8_t> bytes(data, size);
        const AudioDecodeResult first = DecodeWav(bytes);
        if (!first) {
            FUZZ_CHECK(!first.error().empty());
            FUZZ_CHECK(first.error().size() <= kMaxRefusalBytes);
            FUZZ_CHECK(!DecodeWav(bytes).has_value());
            return;
        }
        const DecodedAudio& audio = *first;
        FUZZ_CHECK(audio.format.channels >= 1 && audio.format.channels <= kMaxAudioChannels);
        FUZZ_CHECK(audio.format.sampleRate >= kMinAudioSampleRate && audio.format.sampleRate <= kMaxAudioSampleRate);
        FUZZ_CHECK(!audio.samples.empty());
        FUZZ_CHECK(audio.samples.size() % audio.format.channels == 0);
        // At least one byte per sample was read from the input: nothing is made up past its end
        FUZZ_CHECK(audio.samples.size() <= size);
        for (const float sample : audio.samples) {
            FUZZ_CHECK(std::isfinite(sample));
        }

        // The same bytes give the same sound
        const AudioDecodeResult second = DecodeWav(bytes);
        FUZZ_CHECK(second.has_value());
        FUZZ_CHECK(second->format == audio.format);
        FUZZ_CHECK(second->samples == audio.samples);
    }

}  // namespace BECore::Fuzz
