#include <BECore/Audio/AudioDecode.h>
#include <BECore/AudioTestSupport.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/File/File.h>
#include <Common/JsonDoc.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/ArtGen/ArtGenAccept.h>
#include <Widgets/ArtGen/ArtGenSound.h>
#include <cmath>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        String SoundSettingsText(const ArtGenSoundSettings& settings) {
            String text;
            JsonWriter writer(text);
            WriteArtGenSoundSettings(writer, settings);
            return text;
        }

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

        Path Fixture(const char* name) {
            return AudioFixtureFolder() / name;
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

    TEST(ArtGenSoundTest, TheSettingsAreWrittenAsTheDoublesTheDomStored) {
        ArtGenSoundSettings settings;
        settings.trimDb.reset();
        settings.fadeInMs = 0.1f;
        settings.peakDb = -0.5f;
        EXPECT_EQ(SoundSettingsText(settings), R"({"fadeInMs":0.10000000149011612,"fadeOutMs":30.0,"maxSeconds":0.0,"mono":true,"peakDb":-0.5,"sampleRate":0,"trimDb":null})");
        EXPECT_EQ(SoundSettingsText(ArtGenSoundSettings{}), R"({"fadeInMs":5.0,"fadeOutMs":30.0,"maxSeconds":0.0,"mono":true,"peakDb":-1.0,"sampleRate":0,"trimDb":-50.0})");
    }

    TEST(ArtGenSoundTest, TheSettingsRoundTripThroughJsonAndBadOnesAreNamed) {
        ArtGenSoundSettings settings;
        settings.trimDb.reset();
        settings.sampleRate = 11025;
        settings.maxSeconds = 1.5f;
        settings.fadeInMs = 0.1f;
        const String text = SoundSettingsText(settings);
        const auto back = ReadArtGenSoundSettings(JsonDoc(text));
        ASSERT_TRUE(back.has_value()) << back.error().c_str();
        EXPECT_EQ(*back, settings);
        EXPECT_EQ(*ReadArtGenSoundSettings(JsonDoc("null")), ArtGenSoundSettings{}) << "no settings are the defaults";
        EXPECT_EQ(*ReadArtGenSoundSettings(JsonView()), ArtGenSoundSettings{});
        EXPECT_EQ(*ReadArtGenSoundSettings(JsonDoc("{\"unknown\": 1}")), ArtGenSoundSettings{}) << "an unknown key is ignored";
        EXPECT_EQ(ReadArtGenSoundSettings(JsonDoc("{\"mono\": 1}")).error(), "\"mono\" must be a boolean");
        EXPECT_EQ(ReadArtGenSoundSettings(JsonDoc("[]")).error(), "the sound settings must be an object");
        EXPECT_FALSE(ReadArtGenSoundSettings(JsonDoc("{\"peakDb\": 3}")).has_value());
        EXPECT_FALSE(ReadArtGenSoundSettings(JsonDoc("{\"sampleRate\": 10}")).has_value());
        EXPECT_FALSE(ReadArtGenSoundSettings(JsonDoc("{\"trimDb\": \"loud\"}")).has_value());
        EXPECT_EQ(ReadArtGenSoundSettings(JsonDoc("{\"maxSeconds\": null}")).error(), "\"maxSeconds\" must be a number");
        EXPECT_EQ(ReadArtGenSoundSettings(JsonDoc("{\"peakDb\": \"x\"}")).error(), "\"peakDb\" must be a number or null");
    }

    TEST(ArtGenSoundTest, ANumberThatIsNotFiniteIsRefused) {
        EXPECT_EQ(ReadArtGenSoundSettings(JsonDoc("{\"maxSeconds\": 1e999}")).error(), "\"maxSeconds\" must be a finite number");
        EXPECT_EQ(ReadArtGenSoundSettings(JsonDoc("{\"trimDb\": -1e999}")).error(), "\"trimDb\" must be a finite number");
        EXPECT_FALSE(ReadArtGenSoundSettings(JsonDoc("{\"fadeOutMs\": 1e300}")).has_value()) << "too big for a float: the range check refuses it";
    }

    TEST(ArtGenSoundTest, TheSampleRateIsAWholeNumberClampedToTheLimit) {
        EXPECT_EQ(ReadArtGenSoundSettings(JsonDoc("{\"sampleRate\": 22050}"))->sampleRate, 22050u);
        EXPECT_EQ(ReadArtGenSoundSettings(JsonDoc("{\"sampleRate\": 22050.0}")).error(), "\"sampleRate\" must be a whole number");
        EXPECT_EQ(ReadArtGenSoundSettings(JsonDoc("{\"sampleRate\": -1}")).error(), "\"sampleRate\" must be a whole number");
        EXPECT_FALSE(ReadArtGenSoundSettings(JsonDoc("{\"sampleRate\": 99999999999}")).has_value()) << "clamped to 32 bits, then out of the range of rates";
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
            RemoveDirectory(_root);
            MakeDirectory(_root / "assets" / "sounds");
            _provenance = _root / "pawn_4.artgen.json";
            ASSERT_TRUE(WriteFileAtomically(_provenance, "{\"preset\": \"knock\", \"seed\": 4}").has_value());
        }

        void TearDown() override {
            RemoveDirectory(_root);
        }

        Path _root;
        Path _provenance;
    };

    TEST_F(ArtGenSoundAcceptTest, ASoundBecomesAMonoWavWithItsProvenanceUnderItsName) {
        ArtGenSoundTarget target;
        target.name = "knock";
        const auto written = WriteSoundIntoAssets(Fixture("mono-24bit.flac"), _provenance, _root / "assets", target, ArtGenSoundSettings{});
        ASSERT_TRUE(written.has_value()) << written.error().c_str();
        EXPECT_EQ(written->clipPath, "assets/artgen/knock.wav");
        EXPECT_EQ(written->wavFile, Path(_root / "assets" / "artgen" / "knock.wav"));
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
        EXPECT_EQ(DumpJson(Require(FindJsonMember(*accepted, "processing"))), SoundSettingsText(ArtGenSoundSettings{}));
    }

    TEST_F(ArtGenSoundAcceptTest, TheRecordIsWrittenSortedAndMeasuresAreDoublesNotShortestFloats) {
        ArtGenSoundTarget target;
        target.name = "knock";
        const auto written = WriteSoundIntoAssets(Fixture("mono-24bit.flac"), _provenance, _root / "assets", target, ArtGenSoundSettings{});
        ASSERT_TRUE(written.has_value()) << written.error().c_str();

        const auto doubleText = [](double value) {
            String text;
            JsonWriter writer(text);
            writer.Value(value);
            return text;
        };
        const String text = ReadFile(written->provenanceFile);
        EXPECT_TRUE(StringView(text).starts_with("{\n  \"accepted\": {\n    \"channels\": 1,\n    \"clip\": \"assets/artgen/knock.wav\",\n    \"peakDb\": "));
        // A float widened to a double prints all its digits, as the DOM stored it
        EXPECT_NE(text.find(Format("\"peakDb\": {},\n", doubleText(static_cast<double>(written->peakDb)))), String::npos) << text.c_str();
        EXPECT_NE(text.find(Format("\"seconds\": {},\n", doubleText(written->seconds))), String::npos) << text.c_str();
        EXPECT_TRUE(StringView(text).ends_with("  },\n  \"preset\": \"knock\",\n  \"seed\": 4\n}\n")) << text.c_str();
    }

    TEST_F(ArtGenSoundAcceptTest, AClipOfTheGameIsReplacedAndTheWavDecodes) {
        ArtGenSoundTarget target;
        target.clip = "assets/sounds/move.wav";
        ASSERT_TRUE(WriteFileAtomically(_root / "assets" / "sounds" / "move.wav", "the old clip").has_value());
        ArtGenSoundSettings settings;
        settings.sampleRate = 11025;
        const auto written = WriteSoundIntoAssets(Fixture("mono-24bit.flac"), _provenance, _root / "assets", target, settings);
        ASSERT_TRUE(written.has_value()) << written.error().c_str();
        EXPECT_EQ(written->wavFile, Path(_root / "assets" / "sounds" / "move.wav"));
        EXPECT_TRUE(PathExists(_root / "assets" / "sounds" / "move.artgen.json"));

        const auto decoded = DecodeWav(ReadFileBytes(written->wavFile));
        ASSERT_TRUE(decoded.has_value()) << decoded.error().c_str();
        EXPECT_EQ(decoded->format, (AudioFormat{11025, 1}));
        EXPECT_NEAR(decoded->GetFrameCount() / 11025.0, written->seconds, 1e-3);
    }

    TEST_F(ArtGenSoundAcceptTest, AGameWhoseAssetsFolderIsNonAsciiGetsTheClipAndItsRecord) {
        ArtGenSoundTarget target;
        target.clip = "assets/sounds/move.wav";
        const Path assets(_root / "caf\xC3\xA9" / "assets");
        const auto written = WriteSoundIntoAssets(Fixture("mono-24bit.flac"), _provenance, assets, target, ArtGenSoundSettings{});
        ASSERT_TRUE(written.has_value()) << written.error().c_str();
        EXPECT_EQ(written->wavFile, assets / "sounds" / "move.wav");
        EXPECT_EQ(written->provenanceFile, assets / "sounds" / "move.artgen.json");
        EXPECT_TRUE(GetFileInfo(written->wavFile).has_value());
        EXPECT_TRUE(ReadTextFile(written->provenanceFile.View()).has_value());
        EXPECT_FALSE(PathExists(Path(Format("{}.tmp", written->wavFile.View()))));
    }

    TEST_F(ArtGenSoundAcceptTest, ATargetMustBeOneNameOrOneClip) {
        const auto refused = [&](ArtGenSoundTarget target) { return !WriteSoundIntoAssets(Fixture("mono-24bit.flac"), _provenance, _root / "assets", target, ArtGenSoundSettings{}).has_value(); };
        EXPECT_TRUE(refused(ArtGenSoundTarget{}));
        EXPECT_TRUE(refused(ArtGenSoundTarget{"a", "assets/sounds/a.wav"}));
        EXPECT_TRUE(refused(ArtGenSoundTarget{"../a", ""}));
        EXPECT_TRUE(refused(ArtGenSoundTarget{"", "assets/../a.wav"}));
        EXPECT_FALSE(PathExists(_root / "a.wav"));
    }

}  // namespace BECore::Tests
