#include <Stress/StressSettings.h>
#include <gtest/gtest.h>

namespace BECore::Stress::Tests {

    namespace {

        using Args = Vector<String>;

        std::expected<StressSettings, String> Parse(const Args& args) {
            CommandLine line;
            line.Set(args);
            return ParseStressSettings(line);
        }

        /// The error of @p args, which has to name @p flag
        void ExpectErrorNaming(const Args& args, StringView flag) {
            const auto result = Parse(args);
            ASSERT_FALSE(result.has_value()) << args[0].c_str();
            EXPECT_TRUE(result.error().find(String(flag)) != String::npos) << result.error().c_str();
        }

    }  // namespace

    TEST(StressSettingsTest, NoArgumentsGiveTheDefaults) {
        const auto result = Parse({});
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->scene, StressScene::Sprites);
        EXPECT_EQ(result->count, 2000u);
        EXPECT_EQ(result->frames, 3000u);
        EXPECT_EQ(result->warmup, 120u);
        EXPECT_FALSE(result->framesGiven);
        EXPECT_FALSE(result->ramp);
        EXPECT_FALSE(result->IsMeasuring());
        EXPECT_EQ(result->measure, StressMeasure::Work);
        EXPECT_FALSE(result->interleave);
        EXPECT_EQ(result->atlases, 4u);
        EXPECT_EQ(result->language, StressLanguage::En);
    }

    TEST(StressSettingsTest, OtherFlagsOfTheProgramAreIgnored) {
        const auto result = Parse({"--headless", "--control", "--control-port=0", "--manual-clock", "--profile=trace.json"});
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->count, kDefaultCount);
    }

    TEST(StressSettingsTest, EveryFlagIsRead) {
        const auto result = Parse({"--stress-scene=ui", "--stress-count=300", "--stress-frames=600", "--stress-warmup=0", "--stress-ramp", "--stress-measure=cpu", "--stress-interleave",
                                   "--stress-atlases=8", "--stress-language=ru"});
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->scene, StressScene::Ui);
        EXPECT_EQ(result->count, 300u);
        EXPECT_EQ(result->frames, 600u);
        EXPECT_EQ(result->warmup, 0u);
        EXPECT_TRUE(result->ramp);
        EXPECT_EQ(result->measure, StressMeasure::Cpu);
        EXPECT_TRUE(result->interleave);
        EXPECT_EQ(result->atlases, 8u);
        EXPECT_EQ(result->language, StressLanguage::Ru);
    }

    TEST(StressSettingsTest, AMeasuringRunIsAskedForByFramesOrByTheRamp) {
        const auto fixed = Parse({"--stress-frames=600"});
        ASSERT_TRUE(fixed.has_value());
        EXPECT_TRUE(fixed->framesGiven);
        EXPECT_TRUE(fixed->IsMeasuring());
        const auto ramp = Parse({"--stress-ramp"});
        ASSERT_TRUE(ramp.has_value());
        EXPECT_FALSE(ramp->framesGiven);
        EXPECT_TRUE(ramp->IsMeasuring());
        // The default number of frames alone, or a warm-up alone, starts nothing
        EXPECT_FALSE(Parse({"--stress-warmup=10", "--stress-count=5"})->IsMeasuring());
    }

    TEST(StressSettingsTest, TheSceneIsReadInAnyCase) {
        EXPECT_EQ(Parse({"--stress-scene=Sprites"})->scene, StressScene::Sprites);
        EXPECT_EQ(Parse({"--stress-scene=ENTITIES"})->scene, StressScene::Entities);
        EXPECT_EQ(Parse({"--stress-scene=uI"})->scene, StressScene::Ui);
        EXPECT_EQ(Parse({"--stress-language=RU"})->language, StressLanguage::Ru);
        EXPECT_EQ(Parse({"--stress-measure=Cpu"})->measure, StressMeasure::Cpu);
    }

    TEST(StressSettingsTest, TheBoundsOfTheNumbersAreInclusive) {
        EXPECT_EQ(Parse({"--stress-count=1"})->count, 1u);
        EXPECT_EQ(Parse({"--stress-count=1000000"})->count, kMaxCount);
        EXPECT_EQ(Parse({"--stress-frames=1"})->frames, 1u);
        EXPECT_EQ(Parse({"--stress-frames=1000000"})->frames, kMaxFrames);
        EXPECT_EQ(Parse({"--stress-warmup=1000000"})->warmup, kMaxFrames);
        EXPECT_EQ(Parse({"--stress-atlases=1"})->atlases, 1u);
        EXPECT_EQ(Parse({"--stress-atlases=8"})->atlases, 8u);
    }

    TEST(StressSettingsTest, ANumberOutOfItsBoundsNamesItsFlag) {
        ExpectErrorNaming({"--stress-count=0"}, "--stress-count");
        ExpectErrorNaming({"--stress-count=1000001"}, "--stress-count");
        ExpectErrorNaming({"--stress-frames=0"}, "--stress-frames");
        ExpectErrorNaming({"--stress-frames=1000001"}, "--stress-frames");
        ExpectErrorNaming({"--stress-warmup=1000001"}, "--stress-warmup");
        ExpectErrorNaming({"--stress-atlases=0"}, "--stress-atlases");
        ExpectErrorNaming({"--stress-atlases=9"}, "--stress-atlases");
    }

    TEST(StressSettingsTest, ANumberThatIsNotAWholeNumberNamesItsFlag) {
        ExpectErrorNaming({"--stress-count=abc"}, "--stress-count");
        ExpectErrorNaming({"--stress-count=12x"}, "--stress-count");
        ExpectErrorNaming({"--stress-count=1.5"}, "--stress-count");
        ExpectErrorNaming({"--stress-count=-5"}, "--stress-count");
        ExpectErrorNaming({"--stress-count=+5"}, "--stress-count");
        ExpectErrorNaming({"--stress-count=99999999999999"}, "--stress-count");
        ExpectErrorNaming({"--stress-frames= 5"}, "--stress-frames");
        ExpectErrorNaming({"--stress-warmup=x"}, "--stress-warmup");
    }

    TEST(StressSettingsTest, AnUnknownChoiceNamesItsFlagAndTheChoices) {
        ExpectErrorNaming({"--stress-scene=3d"}, "--stress-scene");
        ExpectErrorNaming({"--stress-measure=wall"}, "--stress-measure");
        ExpectErrorNaming({"--stress-language=de"}, "--stress-language");
        const auto result = Parse({"--stress-scene=3d"});
        EXPECT_TRUE(result.error().find("sprites, entities, ui") != String::npos) << result.error().c_str();
    }

    TEST(StressSettingsTest, AnEmptyValueIsAnError) {
        ExpectErrorNaming({"--stress-count="}, "--stress-count");
        ExpectErrorNaming({"--stress-scene="}, "--stress-scene");
        ExpectErrorNaming({"--stress-language="}, "--stress-language");
    }

    TEST(StressSettingsTest, AValuedFlagWithoutAValueIsAnError) {
        ExpectErrorNaming({"--stress-count"}, "--stress-count");
        ExpectErrorNaming({"--stress-scene"}, "--stress-scene");
        ExpectErrorNaming({"--stress-measure"}, "--stress-measure");
    }

    TEST(StressSettingsTest, ABareFlagWithAValueIsAnError) {
        ExpectErrorNaming({"--stress-ramp=1"}, "--stress-ramp");
        ExpectErrorNaming({"--stress-interleave=yes"}, "--stress-interleave");
    }

    TEST(StressSettingsTest, TheFirstOfARepeatedFlagWins) {
        EXPECT_EQ(Parse({"--stress-count=5", "--stress-count=6"})->count, 5u);
    }

    TEST(StressSettingsTest, TheFlagValuesRoundTrip) {
        for (const StressScene scene : EnumUtils<StressScene>::Values()) {
            EXPECT_EQ(ParseScene(ToFlagValue(scene)), scene);
        }
        for (const StressLanguage language : EnumUtils<StressLanguage>::Values()) {
            EXPECT_EQ(ParseLanguage(ToFlagValue(language)), language);
        }
        EXPECT_EQ(ToFlagValue(StressScene::Entities), "entities");
        EXPECT_EQ(ToFlagValue(StressLanguage::Ru), "ru");
        EXPECT_EQ(ToFlagValue(StressMeasure::Cpu), "cpu");
        EXPECT_FALSE(ParseScene("").has_value());
        EXPECT_FALSE(ParseScene("sprite").has_value());
        EXPECT_FALSE(ParseLanguage("english").has_value());
    }

}  // namespace BECore::Stress::Tests
