#include <BECore/Audio/AudioDecode.h>
#include <Fuzz/FuzzTargets.h>
#include <cmath>

namespace BECore::Fuzz {

    namespace {

        /// What a refusal of the decoder may be: the loader logs it after the file name
        constexpr size_t kMaxReasonBytes = 200;

        /// A FLAC frame of a few bytes may stand for 65535 samples a channel: the target bounds the decode, as the engine's default does
        constexpr size_t kMaxFrames = 1U << 16U;

        /// The chunk the stream is read in: not a divisor of FLAC's block sizes, so a read ends inside a block
        constexpr size_t kStreamChunkFrames = 1000;

    }  // namespace

    void FuzzAudioFlac(const uint8_t* data, size_t size) {
        const Span<const uint8_t> bytes(data, size);
        const AudioDecodeResult first = DecodeFlac(bytes, kMaxFrames);
        if (!first) {
            FUZZ_CHECK(!first.error().empty());
            FUZZ_CHECK(first.error().size() <= kMaxReasonBytes);
            FUZZ_CHECK(!DecodeFlac(bytes, kMaxFrames).has_value());
            return;
        }
        const DecodedAudio& audio = *first;
        FUZZ_CHECK(audio.format.channels >= 1 && audio.format.channels <= kMaxAudioChannels);
        FUZZ_CHECK(audio.format.sampleRate >= kMinAudioSampleRate && audio.format.sampleRate <= kMaxAudioSampleRate);
        FUZZ_CHECK(!audio.samples.empty());
        FUZZ_CHECK(audio.samples.size() % audio.format.channels == 0);
        FUZZ_CHECK(audio.GetFrameCount() <= kMaxFrames);
        for (const float sample : audio.samples) {
            FUZZ_CHECK(std::isfinite(sample));
        }

        // The same bytes give the same sound
        const AudioDecodeResult second = DecodeFlac(bytes, kMaxFrames);
        FUZZ_CHECK(second.has_value());
        FUZZ_CHECK(second->format == audio.format);
        FUZZ_CHECK(second->samples == audio.samples);

        // The stream of music reads the same samples a chunk at a time
        auto stream = OpenAudioStream(Vector<uint8_t>(bytes.begin(), bytes.end()));
        FUZZ_CHECK(stream.has_value());
        FUZZ_CHECK((*stream)->GetFormat() == audio.format);
        const size_t channels = audio.format.channels;
        Vector<float> chunk(kStreamChunkFrames * channels);
        size_t position = 0;
        for (size_t frames = (*stream)->Read(chunk); frames > 0; frames = (*stream)->Read(chunk)) {
            FUZZ_CHECK(frames <= kStreamChunkFrames);
            FUZZ_CHECK(position + frames * channels <= audio.samples.size());
            for (size_t i = 0; i < frames * channels; ++i) {
                FUZZ_CHECK(chunk[i] == audio.samples[position + i]);
            }
            position += frames * channels;
        }
        FUZZ_CHECK(position == audio.samples.size());

        // What the panel keeps of a generated sound reads back the same
        const AudioEncodeResult wav = EncodeWav(audio.samples, audio.format, WavSampleFormat::Float32);
        FUZZ_CHECK(wav.has_value());
        const AudioDecodeResult back = DecodeWav(*wav);
        FUZZ_CHECK(back.has_value());
        FUZZ_CHECK(back->format == audio.format);
        FUZZ_CHECK(back->samples == audio.samples);
    }

}  // namespace BECore::Fuzz
