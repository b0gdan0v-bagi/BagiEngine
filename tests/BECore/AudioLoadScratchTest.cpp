#include <BECore/Audio/AudioClip.h>
#include <BECore/Audio/AudioClipLoader.h>
#include <BECore/Audio/AudioDecode.h>
#include <BECore/AudioTestSupport.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kFixture = "stereo-tone.ogg";
        constexpr size_t kFixtureFrames = 5512;
        constexpr uint64_t kFixtureHash = 6791543127035919109ull;
        constexpr size_t kTooSmallDecoderBytes = 1024;
        constexpr uint32_t kBigClipRate = 22050;
        constexpr size_t kBigClipSamples = 640000;  // 1.2 MB as 16-bit PCM: over the loader's scratch bound

        uint32_t BitsOf(float sample) {
            uint32_t bits = 0;
            std::memcpy(&bits, &sample, sizeof(bits));
            return bits;
        }

        /// FNV-1a over the bit patterns: it changes when any sample changes in its last bit
        uint64_t HashOf(Span<const float> samples) {
            uint64_t hash = 14695981039346656037ull;
            for (const float sample : samples) {
                const uint32_t bits = BitsOf(sample);
                for (uint32_t byte = 0; byte < 4; ++byte) {
                    hash = (hash ^ ((bits >> (8 * byte)) & 0xFFu)) * 1099511628211ull;
                }
            }
            return hash;
        }

        IntrusivePtr<AudioClip> LoadClip(AudioClipLoader& loader, PoolString path) {
            return StaticPointerCast<AudioClip>(loader.LoadSync(path));
        }

        Vector<uint8_t> ReadOgg() {
            return ReadFileBytes(AudioFixtureFolder() / kFixture.data());
        }

        /// The samples of the fixture as the whole-clip decode of stb_vorbis (`stb_vorbis_decode_memory`, 16 bits / 32768) gave them before the
        /// decode pulled chunks: the first and the last frame, and a hash of all
        void ExpectGoldenFixtureSamples(const AudioDecodeResult& decoded) {
            ASSERT_TRUE(decoded.has_value());
            EXPECT_EQ(decoded->format, (AudioFormat{22050, 2}));
            ASSERT_EQ(decoded->GetFrameCount(), kFixtureFrames);
            const Vector<float>& samples = decoded->samples;
            EXPECT_EQ(BitsOf(samples[0]), 0xB9A00000u);
            EXPECT_EQ(BitsOf(samples[1]), 0x39A00000u);
            EXPECT_EQ(BitsOf(samples[2]), 0x3D722000u);
            EXPECT_EQ(BitsOf(samples[3]), 0xBD722000u);
            EXPECT_EQ(BitsOf(samples[samples.size() - 2]), 0xBDA42000u);
            EXPECT_EQ(BitsOf(samples[samples.size() - 1]), 0x3DA42000u);
            EXPECT_EQ(HashOf({samples.data(), samples.size()}), kFixtureHash);
        }

    }  // namespace

    TEST(AudioLoadScratchTest, TheOggSamplesAreTheSameBitForBitAsTheWholeClipDecode) {
        ExpectGoldenFixtureSamples(DecodeOgg(ReadOgg()));
    }

    TEST(AudioLoadScratchTest, AClipThatNeedsMoreThanTheScratchBufferStillDecodesTheSame) {
        ExpectGoldenFixtureSamples(DecodeOgg(ReadOgg(), kTooSmallDecoderBytes));
    }

    TEST(AudioLoadScratchTest, AnOggDecodeKeepsOnlyTheSamplesOnTheHeap) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        const Vector<uint8_t> bytes = ReadOgg();
        AudioDecodeResult decoded = DecodeOgg(bytes);  // the first decode gives the thread's scratch arena its chunk
        const AllocCount count = CountAllocations([&] { decoded = DecodeOgg(bytes); });

        ASSERT_TRUE(decoded.has_value());
        EXPECT_LE(count.count, 2u);
        EXPECT_LT(count.bytes, 2 * decoded->samples.size() * sizeof(float));
    }

    /// Mounts a folder for the loader; the fixture folder or a temporary one
    class AudioLoadScratchLoaderTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            ASSERT_TRUE(CoreManager::GetFileSystem().Mount("test_audio_scratch"_intern, AudioFixtureFolder()));
            ASSERT_TRUE(CoreManager::GetFileSystem().Mount("test_audio_scratch_big"_intern, std::filesystem::path(_folder.PathOf("big.wav").c_str()).parent_path()));
        }

        void TearDown() override {
            CoreManager::GetFileSystem().Unmount("test_audio_scratch"_intern);
            CoreManager::GetFileSystem().Unmount("test_audio_scratch_big"_intern);
        }

        TestTempFolder _folder{"BagiAudioLoadScratchTest"};
    };

    TEST_F(AudioLoadScratchLoaderTest, ALoadedOggClipAllocatesLittleBesidesItsSamples) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        AudioClipLoader loader;
        auto clip = LoadClip(loader, "test_audio_scratch/stereo-tone.ogg"_intern);  // gives the scratch arena its chunk
        const AllocCount count = CountAllocations([&] { clip = LoadClip(loader, "test_audio_scratch/stereo-tone.ogg"_intern); });

        ASSERT_EQ(clip->GetState(), ResourceState::Loaded);
        EXPECT_EQ(clip->GetFrameCount(), kFixtureFrames);
        EXPECT_LT(count.bytes, 2 * clip->GetSamples().size_bytes());
    }

    TEST_F(AudioLoadScratchLoaderTest, AClipFileOverTheScratchBoundLoadsFromTheHeap) {
        Vector<float> samples(kBigClipSamples, 0.25f);
        const auto wav = EncodeWav({samples.data(), samples.size()}, AudioFormat{kBigClipRate, 1}, WavSampleFormat::Pcm16);
        ASSERT_TRUE(wav.has_value());
        ASSERT_GT(wav->size(), size_t{1} * 1024 * 1024);
        _folder.Write("big.wav", String(reinterpret_cast<const char*>(wav->data()), wav->size()));

        AudioClipLoader loader;
        const auto clip = LoadClip(loader, "test_audio_scratch_big/big.wav"_intern);

        ASSERT_EQ(clip->GetState(), ResourceState::Loaded);
        EXPECT_EQ(clip->GetFrameCount(), kBigClipSamples);
        EXPECT_NEAR(clip->GetSamples()[kBigClipSamples / 2], 0.25f, 1e-3f);
    }

}  // namespace BECore::Tests
