#pragma once

#include <BECore/Audio/AudioTypes.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
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

    /// What stb_vorbis may take from the scratch arena for the decoder of one clip (the setup: codebooks, windows; and its temporaries). The
    /// fixture reports 148080 + 7209 bytes; a clip that needs more is opened again on the heap, so this is a speed bound and not a limit
    inline constexpr size_t kOggDecoderScratchBytes = size_t{160} * 1024;

    /// Decodes an Ogg Vorbis file with the vendored stb_vorbis, which trusts its input: only for the assets of the engine and the games.
    /// The decoder works in @p decoderScratchBytes of the thread's scratch arena (a test passes less to take the heap path)
    [[nodiscard]] AudioDecodeResult DecodeOgg(Span<const uint8_t> bytes, size_t decoderScratchBytes = kOggDecoderScratchBytes);

    /// The longest sound DecodeFlac() makes by default: ten minutes at 48 kHz. A FLAC frame of a few bytes can stand for 65535 samples, so a
    /// whole decode is bounded by this and not by the size of the file; longer music is streamed (OpenAudioStream())
    inline constexpr size_t kMaxFlacClipFrames = size_t{48000} * 60 * 10;

    /// Decodes a native FLAC file (what ComfyUI saves) with the vendored dr_flac; refuses one longer than @p maxFrames. Fuzzed (`AudioFlac`),
    /// so it may also read generated files
    [[nodiscard]] AudioDecodeResult DecodeFlac(Span<const uint8_t> bytes, size_t maxFrames = kMaxFlacClipFrames);

    /// DecodeWav(), DecodeOgg() or DecodeFlac() by the magic of the first bytes (`RIFF....WAVE`, `OggS`, `fLaC`)
    [[nodiscard]] AudioDecodeResult DecodeAudio(Span<const uint8_t> bytes);

    /// The sample format EncodeWav() writes: 16-bit PCM (the size of an asset) or 32-bit IEEE float (no loss, for a sound still to be edited)
    CORE_ENUM(WavSampleFormat, uint8_t, Pcm16, Float32)

    using AudioEncodeResult = std::expected<Vector<uint8_t>, String>;

    /**
     * @brief Writes @p samples (interleaved, in @p format) as a RIFF WAVE file that DecodeWav() reads back: the inverse of it.
     *
     * Samples are clamped to -1..1 for PCM and a sample that is not finite is written as 0. Refused: a format outside the limits of a clip, no
     * samples, a count that is not whole frames, a file over 4 GiB. Pure.
     */
    [[nodiscard]] AudioEncodeResult EncodeWav(Span<const float> samples, AudioFormat format, WavSampleFormat sampleFormat);

    /// Samples read a part at a time, as music plays: the source of a stream voice. Main thread only (it keeps decoder state)
    class IAudioSampleSource {
    public:
        virtual ~IAudioSampleSource() = default;

        /// The rate and channels of every sample Read() gives
        [[nodiscard]] virtual AudioFormat GetFormat() const = 0;

        /// Fills the front of @p out with whole frames, interleaved; the frames written, 0 at the end. Allocates nothing
        virtual size_t Read(Span<float> out) = 0;

        /// Back to the first frame; false when the source cannot go back (and then it stays at the end)
        virtual bool Rewind() = 0;
    };

    using AudioSampleSourceResult = std::expected<UniquePtr<IAudioSampleSource>, String>;

    /**
     * @brief A source over the bytes of a file, which it keeps: Ogg Vorbis and FLAC are decoded while they are read (the decoder's state,
     * not the whole track), a WAVE file is decoded whole by DecodeWav() at once.
     *
     * The same limits as a clip (channels, rate); the Ogg side trusts its input as DecodeOgg() does.
     */
    [[nodiscard]] AudioSampleSourceResult OpenAudioStream(Vector<uint8_t> bytes);

}  // namespace BECore
