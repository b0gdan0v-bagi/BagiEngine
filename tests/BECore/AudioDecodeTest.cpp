#include <BECore/Audio/AudioDecode.h>
#include <Common/Printers.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        constexpr uint16_t kPcm = 1;
        constexpr uint16_t kFloat = 3;
        constexpr uint16_t kExtensible = 0xFFFE;
        constexpr uint32_t kRate = 22050;
        constexpr float kTolerance = 1e-6f;

        void PutU16(Vector<uint8_t>& out, uint16_t value) {
            out.push_back(static_cast<uint8_t>(value & 0xFFU));
            out.push_back(static_cast<uint8_t>(value >> 8U));
        }

        void PutU32(Vector<uint8_t>& out, uint32_t value) {
            for (uint32_t shift = 0; shift < 32; shift += 8) {
                out.push_back(static_cast<uint8_t>((value >> shift) & 0xFFU));
            }
        }

        void PutTag(Vector<uint8_t>& out, const char* tag) {
            out.insert(out.end(), tag, tag + 4);
        }

        void PutChunk(Vector<uint8_t>& out, const char* tag, const Vector<uint8_t>& body, uint32_t declaredSize) {
            PutTag(out, tag);
            PutU32(out, declaredSize);
            out.insert(out.end(), body.begin(), body.end());
            if ((body.size() & 1U) != 0) {
                out.push_back(0);
            }
        }

        /// The fields of a fmt chunk; blockAlign 0 means the right one
        struct Fmt {
            uint16_t format = kPcm;
            uint16_t channels = 1;
            uint32_t rate = kRate;
            uint16_t bits = 16;
            uint16_t blockAlign = 0;
            bool extensible = false;
        };

        Vector<uint8_t> FmtBody(const Fmt& fmt) {
            Vector<uint8_t> body;
            const uint16_t align = fmt.blockAlign != 0 ? fmt.blockAlign : static_cast<uint16_t>(fmt.channels * fmt.bits / 8);
            PutU16(body, fmt.extensible ? kExtensible : fmt.format);
            PutU16(body, fmt.channels);
            PutU32(body, fmt.rate);
            PutU32(body, fmt.rate * align);
            PutU16(body, align);
            PutU16(body, fmt.bits);
            if (fmt.extensible) {
                PutU16(body, 22);  // the size of the extension
                PutU16(body, fmt.bits);
                PutU32(body, 0);           // the channel mask
                PutU16(body, fmt.format);  // the first two bytes of the sub-format GUID
                body.insert(body.end(), 14, uint8_t{0});
            }
            return body;
        }

        /// A RIFF WAVE file with @p fmt and @p data, and a chunk before them if @p extra is set
        Vector<uint8_t> MakeWav(const Fmt& fmt, const Vector<uint8_t>& data, const Vector<uint8_t>* extra = nullptr) {
            Vector<uint8_t> chunks;
            if (extra != nullptr) {
                PutChunk(chunks, "LIST", *extra, static_cast<uint32_t>(extra->size()));
            }
            const Vector<uint8_t> fmtBody = FmtBody(fmt);
            PutChunk(chunks, "fmt ", fmtBody, static_cast<uint32_t>(fmtBody.size()));
            PutChunk(chunks, "data", data, static_cast<uint32_t>(data.size()));
            Vector<uint8_t> file;
            PutTag(file, "RIFF");
            PutU32(file, static_cast<uint32_t>(chunks.size() + 4));
            PutTag(file, "WAVE");
            file.insert(file.end(), chunks.begin(), chunks.end());
            return file;
        }

        Vector<uint8_t> Pcm16(std::initializer_list<int16_t> samples) {
            Vector<uint8_t> out;
            for (int16_t sample : samples) {
                PutU16(out, static_cast<uint16_t>(sample));
            }
            return out;
        }

        Vector<uint8_t> Floats(std::initializer_list<float> samples) {
            Vector<uint8_t> out;
            for (float sample : samples) {
                uint32_t bits = 0;
                std::memcpy(&bits, &sample, sizeof(bits));
                PutU32(out, bits);
            }
            return out;
        }

        Vector<uint8_t> ReadFixture(const char* name) {
            const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() / "AudioData" / name;
            std::ifstream file(path, std::ios::binary | std::ios::ate);  //-NoStd: the test reads its fixture from the source tree
            Vector<uint8_t> bytes(file ? static_cast<size_t>(file.tellg()) : 0);
            file.seekg(0);
            file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return bytes;
        }

        String ErrorOf(const AudioDecodeResult& result) {
            return result ? String("decoded") : result.error();
        }

    }  // namespace

    TEST(AudioDecodeTest, Pcm16MonoBecomesFloats) {
        const auto result = DecodeWav(MakeWav(Fmt{}, Pcm16({0, 16384, -32768, 32767})));
        ASSERT_TRUE(result.has_value()) << ErrorOf(result).c_str();
        EXPECT_EQ(result->format, (AudioFormat{kRate, 1}));
        ASSERT_EQ(result->samples.size(), 4u);
        EXPECT_FLOAT_EQ(result->samples[0], 0.0f);
        EXPECT_FLOAT_EQ(result->samples[1], 0.5f);
        EXPECT_FLOAT_EQ(result->samples[2], -1.0f);
        EXPECT_NEAR(result->samples[3], 1.0f, 1e-4f);
    }

    TEST(AudioDecodeTest, EightBitsAreUnsignedAroundTheMiddle) {
        const auto result = DecodeWav(MakeWav(Fmt{.bits = 8}, Vector<uint8_t>{128, 0, 255, 64}));
        ASSERT_TRUE(result.has_value()) << ErrorOf(result).c_str();
        EXPECT_FLOAT_EQ(result->samples[0], 0.0f);
        EXPECT_FLOAT_EQ(result->samples[1], -1.0f);
        EXPECT_NEAR(result->samples[2], 1.0f, 0.01f);
        EXPECT_FLOAT_EQ(result->samples[3], -0.5f);
    }

    TEST(AudioDecodeTest, TwentyFourBitsKeepTheSign) {
        // -1 (0xFFFFFF), the most negative (0x800000) and a half (0x400000), little endian
        const auto result = DecodeWav(MakeWav(Fmt{.bits = 24}, Vector<uint8_t>{0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x80, 0x00, 0x00, 0x40}));
        ASSERT_TRUE(result.has_value()) << ErrorOf(result).c_str();
        ASSERT_EQ(result->samples.size(), 3u);
        EXPECT_NEAR(result->samples[0], -1.0f / 8388608.0f, kTolerance);
        EXPECT_FLOAT_EQ(result->samples[1], -1.0f);
        EXPECT_FLOAT_EQ(result->samples[2], 0.5f);
    }

    TEST(AudioDecodeTest, ThirtyTwoBitIntegers) {
        Vector<uint8_t> data;
        PutU32(data, 0x40000000U);
        PutU32(data, 0x80000000U);
        const auto result = DecodeWav(MakeWav(Fmt{.bits = 32}, data));
        ASSERT_TRUE(result.has_value()) << ErrorOf(result).c_str();
        EXPECT_FLOAT_EQ(result->samples[0], 0.5f);
        EXPECT_FLOAT_EQ(result->samples[1], -1.0f);
    }

    TEST(AudioDecodeTest, FloatsPassAndANonFiniteOneIsSilence) {
        const auto result =
            DecodeWav(MakeWav(Fmt{.format = kFloat, .channels = 2, .bits = 32}, Floats({0.25f, -0.75f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})));
        ASSERT_TRUE(result.has_value()) << ErrorOf(result).c_str();
        EXPECT_EQ(result->format.channels, 2);
        EXPECT_EQ(result->GetFrameCount(), 2u);
        EXPECT_FLOAT_EQ(result->samples[0], 0.25f);
        EXPECT_FLOAT_EQ(result->samples[1], -0.75f);
        EXPECT_FLOAT_EQ(result->samples[2], 0.0f);
        EXPECT_FLOAT_EQ(result->samples[3], 0.0f);
    }

    TEST(AudioDecodeTest, ExtensibleTakesTheSubFormat) {
        const auto pcm = DecodeWav(MakeWav(Fmt{.extensible = true}, Pcm16({16384})));
        ASSERT_TRUE(pcm.has_value()) << ErrorOf(pcm).c_str();
        EXPECT_FLOAT_EQ(pcm->samples[0], 0.5f);
        const auto floats = DecodeWav(MakeWav(Fmt{.format = kFloat, .bits = 32, .extensible = true}, Floats({0.125f})));
        ASSERT_TRUE(floats.has_value()) << ErrorOf(floats).c_str();
        EXPECT_FLOAT_EQ(floats->samples[0], 0.125f);
    }

    TEST(AudioDecodeTest, AChunkOfOddSizeIsSkippedWithItsPadByte) {
        const Vector<uint8_t> extra{1, 2, 3};
        const auto result = DecodeWav(MakeWav(Fmt{}, Pcm16({16384}), &extra));
        ASSERT_TRUE(result.has_value()) << ErrorOf(result).c_str();
        EXPECT_FLOAT_EQ(result->samples[0], 0.5f);
    }

    TEST(AudioDecodeTest, ATrailingPartFrameIsDropped) {
        Vector<uint8_t> data = Pcm16({16384, 16384, 16384});  // a stereo frame and half of the next
        const auto result = DecodeWav(MakeWav(Fmt{.channels = 2}, data));
        ASSERT_TRUE(result.has_value()) << ErrorOf(result).c_str();
        EXPECT_EQ(result->GetFrameCount(), 1u);
        EXPECT_EQ(result->samples.size(), 2u);
    }

    TEST(AudioDecodeTest, MalformedFilesAreRefused) {
        EXPECT_FALSE(DecodeWav(Vector<uint8_t>{}).has_value());
        EXPECT_FALSE(DecodeWav(Pcm16({1, 2, 3, 4, 5, 6})).has_value()) << "no RIFF header";
        EXPECT_FALSE(DecodeWav(MakeWav(Fmt{.channels = 0, .blockAlign = 2}, Pcm16({1}))).has_value());
        EXPECT_FALSE(DecodeWav(MakeWav(Fmt{.channels = 9}, Pcm16({1, 2, 3, 4, 5, 6, 7, 8, 9}))).has_value());
        EXPECT_FALSE(DecodeWav(MakeWav(Fmt{.rate = 999}, Pcm16({1}))).has_value());
        EXPECT_FALSE(DecodeWav(MakeWav(Fmt{.bits = 12, .blockAlign = 2}, Pcm16({1}))).has_value());
        EXPECT_FALSE(DecodeWav(MakeWav(Fmt{.format = kFloat, .bits = 16}, Pcm16({1}))).has_value()) << "16-bit float";
        EXPECT_FALSE(DecodeWav(MakeWav(Fmt{.format = 2}, Pcm16({1}))).has_value()) << "ADPCM";
        EXPECT_FALSE(DecodeWav(MakeWav(Fmt{.blockAlign = 4}, Pcm16({1, 2}))).has_value()) << "a block align that does not fit";
        EXPECT_FALSE(DecodeWav(MakeWav(Fmt{.channels = 2}, Pcm16({1}))).has_value()) << "no whole frame";
    }

    TEST(AudioDecodeTest, AChunkPastTheEndIsRefused) {
        Vector<uint8_t> file = MakeWav(Fmt{}, Pcm16({1, 2}));
        file.resize(file.size() - 1);  // the data chunk now says one byte more than there is
        EXPECT_FALSE(DecodeWav(file).has_value());

        Vector<uint8_t> noData;
        PutTag(noData, "RIFF");
        PutU32(noData, 0);
        PutTag(noData, "WAVE");
        const Vector<uint8_t> fmtBody = FmtBody(Fmt{});
        PutChunk(noData, "fmt ", fmtBody, static_cast<uint32_t>(fmtBody.size()));
        EXPECT_EQ(ErrorOf(DecodeWav(noData)), "no data chunk");
    }

    TEST(AudioDecodeTest, TheOggFixtureDecodes) {
        const Vector<uint8_t> bytes = ReadFixture("stereo-tone.ogg");
        ASSERT_FALSE(bytes.empty());
        const auto result = DecodeOgg(bytes);
        ASSERT_TRUE(result.has_value()) << ErrorOf(result).c_str();
        EXPECT_EQ(result->format, (AudioFormat{kRate, 2}));
        // A quarter of a second; the encoder may pad the last packet a little
        EXPECT_NEAR(static_cast<double>(result->GetFrameCount()), kRate / 4.0, 64.0);
        // The fixture's right channel is the left one inverted
        float largest = 0.0f;
        for (size_t frame = 0; frame < result->GetFrameCount(); ++frame) {
            const float left = result->samples[frame * 2];
            const float right = result->samples[frame * 2 + 1];
            EXPECT_NEAR(left, -right, 0.05f) << "frame " << frame;
            largest = std::max(largest, std::abs(left));
        }
        EXPECT_GT(largest, 0.4f) << "the tone of 0.5 is there";
    }

    TEST(AudioDecodeTest, OggGarbageIsRefused) {
        Vector<uint8_t> bytes{'O', 'g', 'g', 'S'};
        bytes.insert(bytes.end(), 64, uint8_t{0x5A});
        EXPECT_FALSE(DecodeOgg(bytes).has_value());
        EXPECT_FALSE(DecodeOgg(Vector<uint8_t>{}).has_value());
    }

    TEST(AudioDecodeTest, DecodeAudioPicksByTheMagic) {
        const auto wav = DecodeAudio(MakeWav(Fmt{}, Pcm16({1})));
        EXPECT_TRUE(wav.has_value()) << ErrorOf(wav).c_str();
        const auto ogg = DecodeAudio(ReadFixture("stereo-tone.ogg"));
        EXPECT_TRUE(ogg.has_value()) << ErrorOf(ogg).c_str();
        EXPECT_EQ(ErrorOf(DecodeAudio(Vector<uint8_t>{'I', 'D', '3', 4, 0, 0})), "neither a WAVE nor an Ogg file");
    }

    namespace {

        /// Every frame of @p source, read in chunks of @p chunkFrames
        Vector<float> ReadAll(IAudioSampleSource& source, size_t chunkFrames) {
            const size_t channels = source.GetFormat().channels;
            Vector<float> chunk(chunkFrames * channels);
            Vector<float> all;
            for (size_t frames = source.Read(chunk); frames > 0; frames = source.Read(chunk)) {
                all.insert(all.end(), chunk.begin(), chunk.begin() + static_cast<ptrdiff_t>(frames * channels));
            }
            return all;
        }

    }  // namespace

    TEST(AudioDecodeTest, AnOggStreamReadInChunksIsTheWholeDecode) {
        const Vector<uint8_t> bytes = ReadFixture("stereo-tone.ogg");
        const auto whole = DecodeOgg(bytes);
        ASSERT_TRUE(whole.has_value()) << ErrorOf(whole).c_str();
        auto stream = OpenAudioStream(bytes);
        ASSERT_TRUE(stream.has_value()) << stream.error().c_str();
        EXPECT_EQ((*stream)->GetFormat(), whole->format);

        const Vector<float> read = ReadAll(**stream, 1000);  // not a divisor of the length: the last chunk is short
        ASSERT_EQ(read.size(), whole->samples.size());
        // DecodeOgg rounds to 16 bits, the stream gives the decoder's floats
        for (size_t i = 0; i < read.size(); ++i) {
            ASSERT_NEAR(read[i], whole->samples[i], 2.0f / 32768.0f) << "sample " << i;
        }
        Vector<float> after(16);
        EXPECT_EQ((*stream)->Read(after), 0u) << "the end stays the end";
    }

    TEST(AudioDecodeTest, ARewoundOggStreamStartsAgain) {
        auto stream = OpenAudioStream(ReadFixture("stereo-tone.ogg"));
        ASSERT_TRUE(stream.has_value()) << stream.error().c_str();
        const Vector<float> first = ReadAll(**stream, 4096);
        ASSERT_TRUE((*stream)->Rewind());
        const Vector<float> second = ReadAll(**stream, 4096);
        ASSERT_EQ(first.size(), second.size());
        for (size_t i = 0; i < first.size(); ++i) {
            ASSERT_FLOAT_EQ(first[i], second[i]) << "sample " << i;
        }
    }

    TEST(AudioDecodeTest, AWaveStreamReadsTheDecodedSamplesInWholeFrames) {
        auto stream = OpenAudioStream(MakeWav(Fmt{.channels = 2}, Pcm16({0, 16384, -16384, 0, 32767, -32768})));
        ASSERT_TRUE(stream.has_value()) << stream.error().c_str();
        EXPECT_EQ((*stream)->GetFormat(), (AudioFormat{kRate, 2}));
        Vector<float> chunk(5);  // two and a half frames: only whole frames are written
        EXPECT_EQ((*stream)->Read(chunk), 2u);
        EXPECT_FLOAT_EQ(chunk[1], 0.5f);
        EXPECT_FLOAT_EQ(chunk[2], -0.5f);
        EXPECT_EQ((*stream)->Read(chunk), 1u);
        EXPECT_EQ((*stream)->Read(chunk), 0u);
        ASSERT_TRUE((*stream)->Rewind());
        EXPECT_EQ((*stream)->Read(chunk), 2u);
        EXPECT_FLOAT_EQ(chunk[0], 0.0f);
    }

    TEST(AudioDecodeTest, AStreamOfBytesThatAreNotASoundIsRefused) {
        EXPECT_FALSE(OpenAudioStream(Vector<uint8_t>{'I', 'D', '3', 4, 0, 0}).has_value());
        EXPECT_FALSE(OpenAudioStream(Vector<uint8_t>{'O', 'g', 'g', 'S', 0, 2, 0, 0}).has_value());
        EXPECT_FALSE(OpenAudioStream(MakeWav(Fmt{}, Vector<uint8_t>{})).has_value());
        EXPECT_FALSE(OpenAudioStream(Vector<uint8_t>{}).has_value());
    }

}  // namespace BECore::Tests
