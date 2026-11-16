#include <BECore/Audio/AudioDecode.h>
#include <BECore/Control/Json.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenSound.h>
#include <Widgets/EditorLayout/UserFileIo.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr uint32_t kRate = 1000;  // a sample a millisecond keeps the numbers readable

        /// @p silenceFrames of silence, @p loudFrames of a constant @p level (left) and -level (right), then silence again
        DecodedAudio Burst(size_t silenceFrames, size_t loudFrames, float level, uint8_t channels = 2) {
            DecodedAudio audio;
            audio.format = AudioFormat{kRate, channels};
            for (size_t frame = 0; frame < silenceFrames * 2 + loudFrames; ++frame) {
                const bool loud = frame >= silenceFrames && frame < silenceFrames + loudFrames;
                for (uint8_t channel = 0; channel < channels; ++channel) {
                    audio.samples.push_back(loud ? (channel == 0 ? level : level * 0.5f) : 0.0f);
                }
            }
            return audio;
        }

        ArtGenSoundSettings Plain() {
            ArtGenSoundSettings settings;
            settings.mono = false;
            settings.trimDb.reset();
            settings.fadeInMs = 0.0f;
            settings.fadeOutMs = 0.0f;
            settings.peakDb.reset();
            return settings;
        }

        std::filesystem::path Fixture(const char* name) {
            return std::filesystem::path(__FILE__).parent_path() / "AudioData" / name;
        }

    }  // namespace

    TEST(ArtGenSoundTest, PlainSettingsChangeNothing) {
        const DecodedAudio audio = Burst(10, 20, 0.5f);
        const auto result = ProcessArtGenSound(audio, Plain());
        ASSERT_TRUE(result.has_value()) << result.error().c_str();
        EXPECT_EQ(result->format, audio.format);
        EXPECT_EQ(result->samples, audio.samples);
    }

    TEST(ArtGenSoundTest, MonoAveragesTheChannelsAndTrimCutsTheSilenceAtBothEnds) {
        ArtGenSoundSettings settings = Plain();
        settings.mono = true;
        settings.trimDb = -40.0f;
        const auto result = ProcessArtGenSound(Burst(100, 50, 0.8f), settings);
        ASSERT_TRUE(result.has_value()) << result.error().c_str();
        EXPECT_EQ(result->format, (AudioFormat{kRate, 1}));
        ASSERT_EQ(result->GetFrameCount(), 50u);
        EXPECT_FLOAT_EQ(result->samples.front(), 0.6f);  // (0.8 + 0.4) / 2
    }

    TEST(ArtGenSoundTest, MaxLengthCutsAndTheFadesEndInSilence) {
        ArtGenSoundSettings settings = Plain();
        settings.maxSeconds = 0.1f;
        settings.fadeInMs = 10.0f;
        settings.fadeOutMs = 20.0f;
        const auto result = ProcessArtGenSound(Burst(0, 500, 0.5f, 1), settings);
        ASSERT_TRUE(result.has_value()) << result.error().c_str();
        ASSERT_EQ(result->GetFrameCount(), 100u);
        EXPECT_FLOAT_EQ(result->samples[0], 0.0f) << "the fade in starts from silence";
        EXPECT_FLOAT_EQ(result->samples[5], 0.25f);
        EXPECT_FLOAT_EQ(result->samples[50], 0.5f);
        EXPECT_FLOAT_EQ(result->samples[99], 0.0f) << "the last frame is silent: no click";
        EXPECT_FLOAT_EQ(result->samples[89], 0.25f);
    }

    TEST(ArtGenSoundTest, ThePeakIsBroughtToTheTarget) {
        ArtGenSoundSettings settings = Plain();
        settings.peakDb = -6.0f;
        const auto result = ProcessArtGenSound(Burst(0, 10, 0.1f), settings);
        ASSERT_TRUE(result.has_value()) << result.error().c_str();
        const ArtGenSoundStats stats = MeasureArtGenSound(*result);
        EXPECT_NEAR(stats.PeakDb(), -6.0f, 1e-4f);
        EXPECT_NEAR(result->samples[1] / result->samples[0], 0.5f, 1e-6f) << "one gain for every channel";
    }

    TEST(ArtGenSoundTest, ResamplingKeepsTheLength) {
        ArtGenSoundSettings settings = Plain();
        settings.sampleRate = 2000;
        const auto result = ProcessArtGenSound(Burst(0, 300, 0.5f), settings);
        ASSERT_TRUE(result.has_value()) << result.error().c_str();
        EXPECT_EQ(result->format.sampleRate, 2000u);
        EXPECT_EQ(result->GetFrameCount(), 600u);
        EXPECT_FLOAT_EQ(result->samples[200], 0.5f);
    }

    TEST(ArtGenSoundTest, NothingLeftOrABadSettingIsRefused) {
        ArtGenSoundSettings settings = Plain();
        settings.trimDb = -20.0f;
        const auto silent = ProcessArtGenSound(Burst(10, 10, 0.01f), settings);
        ASSERT_FALSE(silent.has_value());
        EXPECT_NE(silent.error().find("quieter than"), String::npos) << silent.error().c_str();
        settings = Plain();
        settings.fadeInMs = -1.0f;
        EXPECT_FALSE(ProcessArtGenSound(Burst(0, 10, 0.5f), settings).has_value());
        EXPECT_FALSE(ProcessArtGenSound(DecodedAudio{}, Plain()).has_value());
    }

    TEST(ArtGenSoundTest, TheSettingsRoundTripThroughJsonAndBadOnesAreNamed) {
        ArtGenSoundSettings settings;
        settings.trimDb.reset();
        settings.sampleRate = 11025;
        settings.maxSeconds = 1.5f;
        const auto back = ArtGenSoundSettingsFromJson(ArtGenSoundSettingsToJson(settings));
        ASSERT_TRUE(back.has_value()) << back.error().c_str();
        EXPECT_EQ(*back, settings);
        EXPECT_EQ(*ArtGenSoundSettingsFromJson(Json()), ArtGenSoundSettings{}) << "no settings are the defaults";
        EXPECT_EQ(ArtGenSoundSettingsFromJson(Json{{"mono", 1}}).error(), "\"mono\" must be a boolean");
        EXPECT_FALSE(ArtGenSoundSettingsFromJson(Json{{"peakDb", 3}}).has_value());
        EXPECT_FALSE(ArtGenSoundSettingsFromJson(Json{{"sampleRate", 10}}).has_value());
        EXPECT_FALSE(ArtGenSoundSettingsFromJson(Json{{"trimDb", "loud"}}).has_value());
        EXPECT_FALSE(ArtGenSoundSettingsFromJson(Json::array()).has_value());
    }

    TEST(ArtGenSoundTest, TheWaveformHasALowAndAHighPerBucket) {
        const DecodedAudio audio = Burst(4, 4, 0.5f, 1);
        const Vector<float> waveform = BuildArtGenWaveform(audio, 3);
        ASSERT_EQ(waveform.size(), 6u);
        EXPECT_FLOAT_EQ(waveform[1], 0.0f) << "the first bucket is silence";
        EXPECT_FLOAT_EQ(waveform[3], 0.5f);
        EXPECT_TRUE(BuildArtGenWaveform(DecodedAudio{}, 10).empty());
        EXPECT_EQ(BuildArtGenWaveform(audio, 100).size(), audio.GetFrameCount() * 2) << "never more buckets than frames";
    }

    TEST(ArtGenSoundTest, OnlyAWavClipInsideTheAssetsIsAPath) {
        EXPECT_TRUE(IsValidClipPath("assets/sounds/move.wav"));
        EXPECT_TRUE(IsValidClipPath("assets/a.b-c_d.wav"));
        EXPECT_FALSE(IsValidClipPath("assets/../config/x.wav"));
        EXPECT_FALSE(IsValidClipPath("assets/sounds/move.ogg"));
        EXPECT_FALSE(IsValidClipPath("/assets/sounds/move.wav"));
        EXPECT_FALSE(IsValidClipPath("sounds/move.wav"));
        EXPECT_FALSE(IsValidClipPath("assets//move.wav"));
        EXPECT_FALSE(IsValidClipPath("assets/.wav"));
        EXPECT_FALSE(IsValidClipPath("assets/sounds/mo ve.wav"));
    }

    class ArtGenSoundAcceptTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _root = TestTempDirectory("artgen_sound");
            std::filesystem::remove_all(_root);
            std::filesystem::create_directories(_root / "assets" / "sounds");
            _provenance = _root / "pawn_4.artgen.json";
            ASSERT_TRUE(WriteTextFileAtomically(_provenance, "{\"preset\": \"knock\", \"seed\": 4}").has_value());
        }

        void TearDown() override {
            std::filesystem::remove_all(_root);
        }

        std::filesystem::path _root;
        std::filesystem::path _provenance;
    };

    TEST_F(ArtGenSoundAcceptTest, ASoundBecomesAMonoWavWithItsProvenanceUnderItsName) {
        ArtGenSoundTarget target;
        target.name = "knock";
        const auto written = WriteSoundIntoAssets(Fixture("mono-24bit.flac"), _provenance, _root / "assets", target, ArtGenSoundSettings{});
        ASSERT_TRUE(written.has_value()) << written.error().c_str();
        EXPECT_EQ(written->clipPath, "assets/artgen/knock.wav");
        EXPECT_EQ(written->wavFile, _root / "assets" / "artgen" / "knock.wav");
        EXPECT_EQ(written->format, (AudioFormat{22050, 1}));
        EXPECT_NEAR(written->peakDb, -1.0f, 0.01f);

        const auto text = ReadTextFile(written->provenanceFile);
        ASSERT_TRUE(text.has_value()) << text.error().c_str();
        const auto provenance = ParseJson(*text);
        ASSERT_TRUE(provenance.has_value());
        EXPECT_EQ(GetJsonInt(*provenance, "seed"), Optional<int64_t>(4)) << "the generation's record is kept";
        const Json* accepted = FindJsonMember(*provenance, "accepted");
        ASSERT_NE(accepted, nullptr);
        EXPECT_EQ(GetJsonString(*accepted, "clip"), Optional<String>("assets/artgen/knock.wav"));
        EXPECT_EQ(*FindJsonMember(*accepted, "processing"), ArtGenSoundSettingsToJson(ArtGenSoundSettings{}));
    }

    TEST_F(ArtGenSoundAcceptTest, AClipOfTheGameIsReplacedAndTheWavDecodes) {
        ArtGenSoundTarget target;
        target.clip = "assets/sounds/move.wav";
        ASSERT_TRUE(WriteTextFileAtomically(_root / "assets" / "sounds" / "move.wav", "the old clip").has_value());
        ArtGenSoundSettings settings;
        settings.sampleRate = 11025;
        const auto written = WriteSoundIntoAssets(Fixture("mono-24bit.flac"), _provenance, _root / "assets", target, settings);
        ASSERT_TRUE(written.has_value()) << written.error().c_str();
        EXPECT_EQ(written->wavFile, _root / "assets" / "sounds" / "move.wav");
        EXPECT_TRUE(std::filesystem::exists(_root / "assets" / "sounds" / "move.artgen.json"));

        std::FILE* stream = std::fopen(written->wavFile.string().c_str(), "rb");
        ASSERT_NE(stream, nullptr);
        Vector<uint8_t> bytes(static_cast<size_t>(std::filesystem::file_size(written->wavFile)));
        ASSERT_EQ(std::fread(bytes.data(), 1, bytes.size(), stream), bytes.size());
        std::fclose(stream);
        const auto decoded = DecodeWav(bytes);
        ASSERT_TRUE(decoded.has_value()) << decoded.error().c_str();
        EXPECT_EQ(decoded->format, (AudioFormat{11025, 1}));
        EXPECT_NEAR(decoded->GetFrameCount() / 11025.0, written->seconds, 1e-3);
    }

    TEST_F(ArtGenSoundAcceptTest, ATargetMustBeOneNameOrOneClip) {
        const auto refused = [&](ArtGenSoundTarget target) { return !WriteSoundIntoAssets(Fixture("mono-24bit.flac"), _provenance, _root / "assets", target, ArtGenSoundSettings{}).has_value(); };
        EXPECT_TRUE(refused(ArtGenSoundTarget{}));
        EXPECT_TRUE(refused(ArtGenSoundTarget{"a", "assets/sounds/a.wav"}));
        EXPECT_TRUE(refused(ArtGenSoundTarget{"../a", ""}));
        EXPECT_TRUE(refused(ArtGenSoundTarget{"", "assets/../a.wav"}));
        EXPECT_FALSE(std::filesystem::exists(_root / "a.wav"));
    }

}  // namespace BECore::Tests
