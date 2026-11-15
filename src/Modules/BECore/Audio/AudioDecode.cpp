#include "AudioDecode.h"

#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>

// The one translation unit that holds the stb_vorbis implementation (src/Modules/stb/README.md). No stdio (the engine reads the bytes) and no
// push-data API (a clip is decoded whole); the functions stay internal to this file.
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
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <stb_vorbis.c>
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

    AudioDecodeResult DecodeAudio(Span<const uint8_t> bytes) {
        if (bytes.size() >= kRiffHeaderSize && HasTag(bytes.data(), "RIFF") && HasTag(bytes.data() + 8, "WAVE")) {
            return DecodeWav(bytes);
        }
        if (bytes.size() >= kTagSize && HasTag(bytes.data(), "OggS")) {
            return DecodeOgg(bytes);
        }
        return std::unexpected(String("neither a WAVE nor an Ogg file"));
    }

}  // namespace BECore
