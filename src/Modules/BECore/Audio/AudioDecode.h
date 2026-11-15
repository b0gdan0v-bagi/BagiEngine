#pragma once

#include <BECore/Audio/AudioTypes.h>
#include <BECore/Types/Types.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /// Samples of a whole sound, interleaved, in `format`; never empty when a decoder returns it
    struct DecodedAudio {
        AudioFormat format;
        Vector<float> samples;

        [[nodiscard]] size_t GetFrameCount() const {
            return format.channels == 0 ? 0 : samples.size() / format.channels;
        }
    };

    /// The sound, or why the bytes are not one: the sentence names what is wrong, not the file
    using AudioDecodeResult = std::expected<DecodedAudio, String>;

    /**
     * @brief Decodes a RIFF WAVE file: PCM of 8, 16, 24 or 32 bits, IEEE float of 32 bits, also inside WAVE_FORMAT_EXTENSIBLE.
     *
     * Every size is checked against @p bytes, so a malformed file is a refusal, never a read past the end; a trailing part frame is dropped and a
     * float sample that is not finite becomes 0. Pure: no file, no service.
     */
    [[nodiscard]] AudioDecodeResult DecodeWav(Span<const uint8_t> bytes);

    /// Decodes an Ogg Vorbis file with the vendored stb_vorbis, which trusts its input: only for the assets of the engine and the games
    [[nodiscard]] AudioDecodeResult DecodeOgg(Span<const uint8_t> bytes);

    /// DecodeWav() or DecodeOgg() by the magic of the first bytes (`RIFF....WAVE`, `OggS`)
    [[nodiscard]] AudioDecodeResult DecodeAudio(Span<const uint8_t> bytes);

}  // namespace BECore
