#include "AudioDecode.h"

#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

// The one translation unit that holds the stb_vorbis implementation (src/Modules/stb/README.md). No stdio (the engine reads the bytes) and no
// push-data API (a clip is decoded whole). stb_vorbis has no static option: its functions (stb_vorbis_decode_memory, ...) have external C
// linkage, so a second copy linked into a game (another library that carries stb_vorbis) clashes at link time.
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#pragma clang diagnostic ignored "-Wold-style-cast"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wcast-qual"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wshadow"
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#pragma clang diagnostic ignored "-Wcomma"
#pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wunused-macros"
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wdouble-promotion"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wextra-semi-stmt"
#pragma clang diagnostic ignored "-Wunreachable-code"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_vorbis.c>
// stb_vorbis.c defines these at file scope for its channel tables
#undef L
#undef C
#undef R
// The one translation unit that holds dr_flac (src/Modules/dr_libs/README.md). Every function static, so another copy in a game links; no
// stdio (the engine reads the bytes), no Ogg FLAC (an `OggS` file is Vorbis here)
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DRFLAC_API static
#define DRFLAC_PRIVATE static
#include <dr_flac.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace BECore {

    namespace {

        constexpr size_t kTagSize = 4;
        constexpr size_t kChunkHeaderSize = 8;
        constexpr size_t kRiffHeaderSize = 12;
        constexpr size_t kFmtMinSize = 16;
        constexpr size_t kFmtExtensibleSize = 40;
        constexpr size_t kExtensibleSubFormatOffset = 24;

        constexpr uint16_t kFormatPcm = 1;
        constexpr uint16_t kFormatFloat = 3;
        constexpr uint16_t kFormatExtensible = 0xFFFE;

        constexpr uint16_t kBits8 = 8;
        constexpr uint16_t kBits16 = 16;
        constexpr uint16_t kBits24 = 24;
        constexpr uint16_t kBits32 = 32;
        constexpr uint16_t kBitsPerByte = 8;

        constexpr float kScale8 = 128.0f;
        constexpr float kScale16 = 32768.0f;
        constexpr float kScale24 = 8388608.0f;
        constexpr float kScale32 = 2147483648.0f;
        constexpr int kUnsigned8Offset = 128;
        constexpr int kSignExtend24Shift = 8;

        uint16_t ReadU16(const uint8_t* p) {
            return static_cast<uint16_t>(p[0] | (p[1] << kBitsPerByte));
        }

        uint32_t ReadU32(const uint8_t* p) {
            return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8U) | (static_cast<uint32_t>(p[2]) << 16U) | (static_cast<uint32_t>(p[3]) << 24U);
        }

        bool HasTag(const uint8_t* p, const char* tag) {
            return std::memcmp(p, tag, kTagSize) == 0;
        }

        struct WavFmt {
            uint16_t format = 0;
            uint16_t channels = 0;
            uint32_t sampleRate = 0;
            uint16_t blockAlign = 0;
            uint16_t bitsPerSample = 0;
        };

        std::expected<WavFmt, String> ParseFmt(const uint8_t* body, size_t size) {
            if (size < kFmtMinSize) {
                return std::unexpected(String("the fmt chunk is shorter than 16 bytes"));
            }
            WavFmt fmt;
            fmt.format = ReadU16(body);
            fmt.channels = ReadU16(body + 2);
            fmt.sampleRate = ReadU32(body + 4);
            fmt.blockAlign = ReadU16(body + 12);
            fmt.bitsPerSample = ReadU16(body + 14);
            if (fmt.format == kFormatExtensible) {
                if (size < kFmtExtensibleSize) {
                    return std::unexpected(String("the fmt chunk of WAVE_FORMAT_EXTENSIBLE is shorter than 40 bytes"));
                }
                // The first two bytes of the sub-format GUID are the format code (KSDATAFORMAT_SUBTYPE_PCM / _IEEE_FLOAT)
                fmt.format = ReadU16(body + kExtensibleSubFormatOffset);
            }
            if (fmt.format != kFormatPcm && fmt.format != kFormatFloat) {
                return std::unexpected(Format("the sample format {} is neither PCM nor IEEE float", fmt.format));
            }
            if (fmt.channels == 0 || fmt.channels > kMaxAudioChannels) {
                return std::unexpected(Format("{} channels, not 1 to {}", fmt.channels, kMaxAudioChannels));
            }
            if (fmt.sampleRate < kMinAudioSampleRate || fmt.sampleRate > kMaxAudioSampleRate) {
                return std::unexpected(Format("the sample rate {} is not {} to {}", fmt.sampleRate, kMinAudioSampleRate, kMaxAudioSampleRate));
            }
            const bool pcmBits = fmt.bitsPerSample == kBits8 || fmt.bitsPerSample == kBits16 || fmt.bitsPerSample == kBits24 || fmt.bitsPerSample == kBits32;
            if ((fmt.format == kFormatPcm && !pcmBits) || (fmt.format == kFormatFloat && fmt.bitsPerSample != kBits32)) {
                return std::unexpected(Format("{} bits per sample is not supported for this format", fmt.bitsPerSample));
            }
            if (fmt.blockAlign != fmt.channels * (fmt.bitsPerSample / kBitsPerByte)) {
                return std::unexpected(Format("the block align {} does not match {} channels of {} bits", fmt.blockAlign, fmt.channels, fmt.bitsPerSample));
            }
            return fmt;
        }

        float ReadSample(const uint8_t* p, const WavFmt& fmt) {
            if (fmt.format == kFormatFloat) {
                float value = 0.0f;
                std::memcpy(&value, p, sizeof(value));
                return std::isfinite(value) ? value : 0.0f;
            }
            switch (fmt.bitsPerSample) {
                case kBits8:
                    return static_cast<float>(static_cast<int>(p[0]) - kUnsigned8Offset) / kScale8;
                case kBits16:
                    return static_cast<float>(static_cast<int16_t>(ReadU16(p))) / kScale16;
                case kBits24: {
                    // The three bytes go to the top of a 32-bit word, so the arithmetic shift back carries the sign
                    const auto word = static_cast<int32_t>((static_cast<uint32_t>(p[0]) << 8U) | (static_cast<uint32_t>(p[1]) << 16U) | (static_cast<uint32_t>(p[2]) << 24U));
                    return static_cast<float>(word >> kSignExtend24Shift) / kScale24;
                }
                default:
                    return static_cast<float>(static_cast<int32_t>(ReadU32(p))) / kScale32;
            }
        }

    }  // namespace

    AudioDecodeResult DecodeWav(Span<const uint8_t> bytes) {
        if (bytes.size() < kRiffHeaderSize || !HasTag(bytes.data(), "RIFF") || !HasTag(bytes.data() + 8, "WAVE")) {
            return std::unexpected(String("not a RIFF WAVE file"));
        }
        Optional<WavFmt> fmt;
        Span<const uint8_t> data;
        bool haveData = false;
        // The size in the RIFF header is not trusted: the chunks are walked to the end of the bytes
        size_t pos = kRiffHeaderSize;
        while (bytes.size() - pos >= kChunkHeaderSize) {
            const uint8_t* header = bytes.data() + pos;
            const size_t size = ReadU32(header + kTagSize);
            const size_t bodyStart = pos + kChunkHeaderSize;
            const size_t available = bytes.size() - bodyStart;
            if (HasTag(header, "fmt ")) {
                if (size > available) {
                    return std::unexpected(String("the fmt chunk runs past the end of the file"));
                }
                auto parsed = ParseFmt(bytes.data() + bodyStart, size);
                if (!parsed) {
                    return std::unexpected(std::move(parsed.error()));
                }
                fmt = *parsed;
            } else if (HasTag(header, "data") && !haveData) {
                if (size > available) {
                    return std::unexpected(String("the data chunk runs past the end of the file"));
                }
                data = bytes.subspan(bodyStart, size);
                haveData = true;
            }
            if (size > available) {
                break;  // a chunk nobody reads may be cut off at the end
            }
            // A chunk of odd size is followed by one pad byte
            pos = bodyStart + size + (size & 1U);
            if (pos > bytes.size()) {
                break;
            }
        }
        if (!fmt) {
            return std::unexpected(String("no fmt chunk"));
        }
        if (!haveData) {
            return std::unexpected(String("no data chunk"));
        }
        const size_t frames = data.size() / fmt->blockAlign;
        if (frames == 0) {
            return std::unexpected(String("the data chunk holds no whole sample frame"));
        }

        DecodedAudio decoded;
        decoded.format = AudioFormat{fmt->sampleRate, static_cast<uint8_t>(fmt->channels)};
        const size_t sampleBytes = fmt->bitsPerSample / kBitsPerByte;
        const size_t count = frames * fmt->channels;
        decoded.samples.resize(count);
        for (size_t i = 0; i < count; ++i) {
            decoded.samples[i] = ReadSample(data.data() + i * sampleBytes, *fmt);
        }
        return decoded;
    }

    AudioDecodeResult DecodeOgg(Span<const uint8_t> bytes) {
        if (bytes.size() > static_cast<size_t>(INT_MAX)) {
            return std::unexpected(String("the Ogg file is larger than stb_vorbis can read"));
        }
        int channels = 0;
        int sampleRate = 0;
        short* output = nullptr;
        const int frames = stb_vorbis_decode_memory(bytes.data(), static_cast<int>(bytes.size()), &channels, &sampleRate, &output);
        if (frames <= 0 || output == nullptr) {
            std::free(output);  // NOLINT(cppcoreguidelines-no-malloc): stb_vorbis allocates the output with malloc
            return std::unexpected(String("not an Ogg Vorbis file stb_vorbis can decode"));
        }
        if (channels <= 0 || channels > kMaxAudioChannels || sampleRate < static_cast<int>(kMinAudioSampleRate) || sampleRate > static_cast<int>(kMaxAudioSampleRate)) {
            std::free(output);  // NOLINT(cppcoreguidelines-no-malloc): stb_vorbis allocates the output with malloc
            return std::unexpected(Format("{} channels at {} Hz is not a clip the engine plays", channels, sampleRate));
        }

        DecodedAudio decoded;
        decoded.format = AudioFormat{static_cast<uint32_t>(sampleRate), static_cast<uint8_t>(channels)};
        const size_t count = static_cast<size_t>(frames) * static_cast<size_t>(channels);
        decoded.samples.resize(count);
        for (size_t i = 0; i < count; ++i) {
            decoded.samples[i] = static_cast<float>(output[i]) / kScale16;
        }
        std::free(output);  // NOLINT(cppcoreguidelines-no-malloc): stb_vorbis allocates the output with malloc
        return decoded;
    }

    namespace {

        /// Frames read from dr_flac at a time while a whole file is decoded
        constexpr size_t kFlacReadFrames = 4096;

        /// Closes the dr_flac decoder it holds
        class FlacDecoder {
        public:
            explicit FlacDecoder(drflac* decoder) : _decoder(decoder) {}

            ~FlacDecoder() {
                if (_decoder != nullptr) {
                    drflac_close(_decoder);
                }
            }

            FlacDecoder(FlacDecoder&& other) noexcept : _decoder(std::exchange(other._decoder, nullptr)) {}
            FlacDecoder(const FlacDecoder&) = delete;
            FlacDecoder& operator=(const FlacDecoder&) = delete;
            FlacDecoder& operator=(FlacDecoder&&) = delete;

            [[nodiscard]] drflac* Get() const {
                return _decoder;
            }

        private:
            drflac* _decoder = nullptr;
        };

        /// Opens @p bytes (which must outlive the decoder) and checks the stream against the limits of a clip
        std::expected<FlacDecoder, String> OpenFlac(Span<const uint8_t> bytes) {
            FlacDecoder decoder(drflac_open_memory(bytes.data(), bytes.size(), nullptr));
            if (decoder.Get() == nullptr) {
                return std::unexpected(String("not a FLAC file dr_flac can open"));
            }
            const drflac& info = *decoder.Get();
            if (info.channels == 0 || info.channels > kMaxAudioChannels || info.sampleRate < kMinAudioSampleRate || info.sampleRate > kMaxAudioSampleRate) {
                return std::unexpected(Format("{} channels at {} Hz is not a sound the engine plays", info.channels, info.sampleRate));
            }
            return decoder;
        }

        void ZeroNonFinite(Span<float> samples) {
            for (float& sample : samples) {
                if (!std::isfinite(sample)) {
                    sample = 0.0f;
                }
            }
        }

    }  // namespace

    AudioDecodeResult DecodeFlac(Span<const uint8_t> bytes, size_t maxFrames) {
        auto opened = OpenFlac(bytes);
        if (!opened) {
            return std::unexpected(std::move(opened.error()));
        }
        drflac* decoder = opened->Get();
        DecodedAudio decoded;
        decoded.format = AudioFormat{decoder->sampleRate, decoder->channels};
        const size_t channels = decoder->channels;
        // The frame count of the header is not trusted (it may be 0 or a lie): the vector grows by what the decoder gives, one frame past the
        // limit at most, so a file over it is seen
        size_t frames = 0;
        while (true) {
            const size_t want = eastl::min(kFlacReadFrames, maxFrames - frames + 1);
            decoded.samples.resize((frames + want) * channels);
            const auto read = static_cast<size_t>(drflac_read_pcm_frames_f32(decoder, want, decoded.samples.data() + frames * channels));
            frames += read;
            if (frames > maxFrames) {
                return std::unexpected(Format("longer than {} frames", maxFrames));
            }
            if (read < want) {
                break;
            }
        }
        if (frames == 0) {
            return std::unexpected(String("the FLAC file holds no sample frame"));
        }
        decoded.samples.resize(frames * channels);
        ZeroNonFinite(decoded.samples);
        return decoded;
    }

    namespace {

        constexpr uint16_t kWavFmtSize = 16;
        constexpr size_t kWavHeaderSize = kRiffHeaderSize + kChunkHeaderSize + kWavFmtSize + kChunkHeaderSize;
        constexpr float kEncodeScale16 = 32767.0f;
        constexpr uint32_t kByteMask = 0xFFU;
        constexpr uint32_t kBitsPerWord = 32;

        void PutU16(Vector<uint8_t>& out, uint16_t value) {
            out.push_back(static_cast<uint8_t>(value & kByteMask));
            out.push_back(static_cast<uint8_t>(value >> kBitsPerByte));
        }

        void PutU32(Vector<uint8_t>& out, uint32_t value) {
            for (uint32_t shift = 0; shift < kBitsPerWord; shift += kBitsPerByte) {
                out.push_back(static_cast<uint8_t>((value >> shift) & kByteMask));
            }
        }

        void PutTag(Vector<uint8_t>& out, const char* tag) {
            out.insert(out.end(), tag, tag + kTagSize);
        }

    }  // namespace

    AudioEncodeResult EncodeWav(Span<const float> samples, AudioFormat format, WavSampleFormat sampleFormat) {
        if (format.channels == 0 || format.channels > kMaxAudioChannels) {
            return std::unexpected(Format("{} channels, not 1 to {}", format.channels, kMaxAudioChannels));
        }
        if (format.sampleRate < kMinAudioSampleRate || format.sampleRate > kMaxAudioSampleRate) {
            return std::unexpected(Format("the sample rate {} is not {} to {}", format.sampleRate, kMinAudioSampleRate, kMaxAudioSampleRate));
        }
        if (samples.empty() || samples.size() % format.channels != 0) {
            return std::unexpected(Format("{} samples are not whole frames of {} channels", samples.size(), format.channels));
        }
        const bool pcm = sampleFormat == WavSampleFormat::Pcm16;
        const uint16_t bits = pcm ? kBits16 : kBits32;
        const size_t sampleBytes = bits / kBitsPerByte;
        const size_t dataSize = samples.size() * sampleBytes;
        if (dataSize > UINT32_MAX - kWavHeaderSize) {
            return std::unexpected(String("the sound is too long for a WAVE file (over 4 GiB)"));
        }
        const auto blockAlign = static_cast<uint16_t>(format.channels * sampleBytes);

        Vector<uint8_t> out;
        out.reserve(kWavHeaderSize + dataSize);
        PutTag(out, "RIFF");
        PutU32(out, static_cast<uint32_t>(kWavHeaderSize - kChunkHeaderSize + dataSize));
        PutTag(out, "WAVE");
        PutTag(out, "fmt ");
        PutU32(out, kWavFmtSize);
        PutU16(out, pcm ? kFormatPcm : kFormatFloat);
        PutU16(out, format.channels);
        PutU32(out, format.sampleRate);
        PutU32(out, format.sampleRate * blockAlign);
        PutU16(out, blockAlign);
        PutU16(out, bits);
        PutTag(out, "data");
        PutU32(out, static_cast<uint32_t>(dataSize));
        for (const float sample : samples) {
            const float value = std::isfinite(sample) ? sample : 0.0f;
            if (pcm) {
                const auto quantized = static_cast<int16_t>(std::lrint(eastl::clamp(value, -1.0f, 1.0f) * kEncodeScale16));
                PutU16(out, static_cast<uint16_t>(quantized));
            } else {
                uint32_t word = 0;
                std::memcpy(&word, &value, sizeof(word));
                PutU32(out, word);
            }
        }
        return out;
    }

    namespace {

        bool IsWave(Span<const uint8_t> bytes) {
            return bytes.size() >= kRiffHeaderSize && HasTag(bytes.data(), "RIFF") && HasTag(bytes.data() + 8, "WAVE");
        }

        bool IsOgg(Span<const uint8_t> bytes) {
            return bytes.size() >= kTagSize && HasTag(bytes.data(), "OggS");
        }

        bool IsFlac(Span<const uint8_t> bytes) {
            return bytes.size() >= kTagSize && HasTag(bytes.data(), "fLaC");
        }

        /// A sound decoded whole, read from memory
        class MemorySampleSource final : public IAudioSampleSource {
        public:
            explicit MemorySampleSource(DecodedAudio audio) : _audio(std::move(audio)) {}

            [[nodiscard]] AudioFormat GetFormat() const override {
                return _audio.format;
            }

            size_t Read(Span<float> out) override {
                const size_t channels = _audio.format.channels;
                const size_t left = _audio.samples.size() - _position;
                const size_t count = eastl::min(out.size() / channels * channels, left);
                std::memcpy(out.data(), _audio.samples.data() + _position, count * sizeof(float));
                _position += count;
                return count / channels;
            }

            bool Rewind() override {
                _position = 0;
                return true;
            }

        private:
            DecodedAudio _audio;
            size_t _position = 0;  ///< in samples, a multiple of the channels
        };

        /// Ogg Vorbis decoded as it is read, through the pull API of stb_vorbis over the bytes it keeps
        class OggSampleSource final : public IAudioSampleSource {
        public:
            OggSampleSource(Vector<uint8_t> bytes, stb_vorbis* decoder, AudioFormat format) : _bytes(std::move(bytes)), _decoder(decoder), _format(format) {}

            ~OggSampleSource() override {
                stb_vorbis_close(_decoder);
            }

            OggSampleSource(const OggSampleSource&) = delete;
            OggSampleSource& operator=(const OggSampleSource&) = delete;

            [[nodiscard]] AudioFormat GetFormat() const override {
                return _format;
            }

            size_t Read(Span<float> out) override {
                const size_t channels = _format.channels;
                const size_t floats = eastl::min(out.size() / channels * channels, static_cast<size_t>(INT_MAX) / channels * channels);
                if (floats == 0) {
                    return 0;
                }
                const int frames = stb_vorbis_get_samples_float_interleaved(_decoder, static_cast<int>(channels), out.data(), static_cast<int>(floats));
                return frames > 0 ? static_cast<size_t>(frames) : 0;
            }

            bool Rewind() override {
                return stb_vorbis_seek_start(_decoder) != 0;
            }

        private:
            Vector<uint8_t> _bytes;  ///< stb_vorbis reads from these: they live as long as the decoder
            stb_vorbis* _decoder = nullptr;
            AudioFormat _format;
        };

        /// FLAC decoded as it is read, by dr_flac over the bytes it keeps
        class FlacSampleSource final : public IAudioSampleSource {
        public:
            FlacSampleSource(Vector<uint8_t> bytes, FlacDecoder decoder) : _bytes(std::move(bytes)), _decoder(std::move(decoder)) {}

            [[nodiscard]] AudioFormat GetFormat() const override {
                return AudioFormat{_decoder.Get()->sampleRate, _decoder.Get()->channels};
            }

            size_t Read(Span<float> out) override {
                const size_t channels = _decoder.Get()->channels;
                const size_t frames = out.size() / channels;
                if (frames == 0) {
                    return 0;
                }
                const auto read = static_cast<size_t>(drflac_read_pcm_frames_f32(_decoder.Get(), frames, out.data()));
                ZeroNonFinite(out.first(read * channels));
                return read;
            }

            bool Rewind() override {
                return drflac_seek_to_pcm_frame(_decoder.Get(), 0) != DRFLAC_FALSE;
            }

        private:
            Vector<uint8_t> _bytes;  ///< dr_flac reads from these: declared first, so they outlive the decoder
            FlacDecoder _decoder;
        };

    }  // namespace

    AudioDecodeResult DecodeAudio(Span<const uint8_t> bytes) {
        if (IsWave(bytes)) {
            return DecodeWav(bytes);
        }
        if (IsOgg(bytes)) {
            return DecodeOgg(bytes);
        }
        if (IsFlac(bytes)) {
            return DecodeFlac(bytes);
        }
        return std::unexpected(String("neither a WAVE, an Ogg nor a FLAC file"));
    }

    AudioSampleSourceResult OpenAudioStream(Vector<uint8_t> bytes) {
        if (IsWave(bytes)) {
            auto decoded = DecodeWav(bytes);
            if (!decoded) {
                return std::unexpected(std::move(decoded.error()));
            }
            return UniquePtr<IAudioSampleSource>(MakeUnique<MemorySampleSource>(std::move(*decoded)));
        }
        if (IsFlac(bytes)) {
            auto opened = OpenFlac(bytes);
            if (!opened) {
                return std::unexpected(std::move(opened.error()));
            }
            // The decoder points into the heap block of the vector, which the move keeps in place
            return UniquePtr<IAudioSampleSource>(MakeUnique<FlacSampleSource>(std::move(bytes), std::move(*opened)));
        }
        if (!IsOgg(bytes)) {
            return std::unexpected(String("neither a WAVE, an Ogg nor a FLAC file"));
        }
        if (bytes.size() > static_cast<size_t>(INT_MAX)) {
            return std::unexpected(String("the Ogg file is larger than stb_vorbis can read"));
        }
        int error = 0;
        stb_vorbis* decoder = stb_vorbis_open_memory(bytes.data(), static_cast<int>(bytes.size()), &error, nullptr);
        if (decoder == nullptr) {
            return std::unexpected(Format("not an Ogg Vorbis file stb_vorbis can open (error {})", error));
        }
        const stb_vorbis_info info = stb_vorbis_get_info(decoder);
        if (info.channels <= 0 || info.channels > kMaxAudioChannels || info.sample_rate < kMinAudioSampleRate || info.sample_rate > kMaxAudioSampleRate) {
            stb_vorbis_close(decoder);
            return std::unexpected(Format("{} channels at {} Hz is not a stream the engine plays", info.channels, info.sample_rate));
        }
        const AudioFormat format{info.sample_rate, static_cast<uint8_t>(info.channels)};
        return UniquePtr<IAudioSampleSource>(MakeUnique<OggSampleSource>(std::move(bytes), decoder, format));
    }

}  // namespace BECore
