#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Control/ControlSettings.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using Args = Vector<String>;

        /// The ApplicationConfig the engine reads from @p xml (the code path of the real start-up, strict values)
        ApplicationConfigLoad LoadFrom(const char* xml) {
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(xml)) << xml;
            return LoadApplicationConfig(deserializer);
        }

        std::expected<ControlSettings, ControlError> Resolve(const Args& args, const char* configXml = nullptr) {
            if (configXml == nullptr) {
                return ResolveControlSettings(args, ApplicationConfigLoad{});
            }
            return ResolveControlSettings(args, LoadFrom(configXml));
        }

        void ExpectInvalid(const std::expected<ControlSettings, ControlError>& result) {
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error().code, ControlErrors::kInvalidParams);
            EXPECT_FALSE(result.error().message.empty());
        }

    }  // namespace

    TEST(ControlSettingsTest, IsOffWithDefaultPortByDefault) {
        const auto settings = Resolve({});
        ASSERT_TRUE(settings.has_value());
        EXPECT_FALSE(settings->enabled);
        EXPECT_EQ(settings->port, kDefaultControlPort);
    }

    TEST(ControlSettingsTest, ConfigWithoutAttributesChangesNothing) {
        const auto settings = Resolve({}, R"(<root type="SDL3ApplicationFabric" mode="Game"/>)");
        ASSERT_TRUE(settings.has_value());
        EXPECT_FALSE(settings->enabled);
        EXPECT_EQ(settings->port, kDefaultControlPort);
    }

    TEST(ControlSettingsTest, ABadModeAloneIsNoControlError) {
        // The mode is not this function's business: Application logs it once and falls back to Editor
        const auto settings = Resolve({}, R"(<root mode="Window"/>)");
        ASSERT_TRUE(settings.has_value());
        EXPECT_FALSE(settings->enabled);
        EXPECT_EQ(settings->port, kDefaultControlPort);

        const auto withFlag = Resolve({"--control"}, R"(<root mode="Window" type="" control="false"/>)");
        ASSERT_TRUE(withFlag.has_value());
        EXPECT_TRUE(withFlag->enabled);
    }

    TEST(ControlSettingsTest, TheErrorNamesTheFieldOfTheConfig) {
        const auto result = Resolve({}, R"(<root controlPort="70000"/>)");
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().code, ControlErrors::kInvalidParams);
        EXPECT_NE(result.error().message.find("ApplicationConfig: controlPort:"), String::npos) << result.error().message.c_str();
    }

    TEST(ControlSettingsTest, CommandLineEnables) {
        const auto settings = Resolve({"--control"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_TRUE(settings->enabled);
        EXPECT_EQ(settings->port, kDefaultControlPort);
    }

    TEST(ControlSettingsTest, CommandLineSetsThePort) {
        const auto settings = Resolve({"--control", "--control-port=9001"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_TRUE(settings->enabled);
        EXPECT_EQ(settings->port, 9001);
    }

    TEST(ControlSettingsTest, PortOrderOnTheCommandLineDoesNotMatter) {
        const auto settings = Resolve({"--control-port=9001", "--control"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_TRUE(settings->enabled);
        EXPECT_EQ(settings->port, 9001);
    }

    TEST(ControlSettingsTest, PortZeroAndTheLargestPortAreValid) {
        const auto ephemeral = Resolve({"--control", "--control-port=0"});
        ASSERT_TRUE(ephemeral.has_value());
        EXPECT_EQ(ephemeral->port, 0);

        const auto largest = Resolve({"--control", "--control-port=65535"});
        ASSERT_TRUE(largest.has_value());
        EXPECT_EQ(largest->port, 65535);
    }

    TEST(ControlSettingsTest, AttributesEnableAndSetThePort) {
        const auto settings = Resolve({}, R"(<root control="true" controlPort="9100"/>)");
        ASSERT_TRUE(settings.has_value());
        EXPECT_TRUE(settings->enabled);
        EXPECT_EQ(settings->port, 9100);
    }

    TEST(ControlSettingsTest, AttributeBooleanAcceptsTheXmlSpellings) {
        for (const char* xml : {R"(<root control="true"/>)", R"(<root control="1"/>)"}) {
            const auto on = Resolve({}, xml);
            ASSERT_TRUE(on.has_value()) << xml;
            EXPECT_TRUE(on->enabled) << xml;
        }
        for (const char* xml : {R"(<root control="false"/>)", R"(<root control="0"/>)"}) {
            const auto off = Resolve({}, xml);
            ASSERT_TRUE(off.has_value()) << xml;
            EXPECT_FALSE(off->enabled) << xml;
        }
    }

    TEST(ControlSettingsTest, CommandLineEnablesOverAnAttributeThatSaysOff) {
        const auto settings = Resolve({"--control"}, R"(<root control="false"/>)");
        ASSERT_TRUE(settings.has_value());
        EXPECT_TRUE(settings->enabled);
    }

    TEST(ControlSettingsTest, CommandLinePortOverridesTheAttributePort) {
        const auto settings = Resolve({"--control-port=9200"}, R"(<root control="true" controlPort="9100"/>)");
        ASSERT_TRUE(settings.has_value());
        EXPECT_TRUE(settings->enabled);
        EXPECT_EQ(settings->port, 9200);
    }

    TEST(ControlSettingsTest, AttributePortAloneDoesNotEnableTheApi) {
        const auto settings = Resolve({}, R"(<root controlPort="9100"/>)");
        ASSERT_TRUE(settings.has_value());
        EXPECT_FALSE(settings->enabled);
        EXPECT_EQ(settings->port, 9100);
    }

    TEST(ControlSettingsTest, LastPortOnTheCommandLineWins) {
        const auto settings = Resolve({"--control", "--control-port=9001", "--control-port=9002"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_EQ(settings->port, 9002);
    }

    TEST(ControlSettingsTest, PortThatIsNotAPortIsAnError) {
        for (const char* value : {"abc", "70000", "65536", "-1", "+80", "", "80x", " 80", "8.5"}) {
            const String arg = Format("--control-port={}", value);
            SCOPED_TRACE(arg.c_str());
            ExpectInvalid(Resolve({"--control", arg}));
        }
    }

    TEST(ControlSettingsTest, BadPortInTheConfigIsAnErrorEvenWhenTheApiIsOff) {
        ExpectInvalid(Resolve({}, R"(<root controlPort="abc"/>)"));
        ExpectInvalid(Resolve({}, R"(<root controlPort="70000"/>)"));
        ExpectInvalid(Resolve({}, R"(<root controlPort=""/>)"));
    }

    TEST(ControlSettingsTest, BadBooleanInTheConfigIsAnError) {
        ExpectInvalid(Resolve({}, R"(<root control="yes"/>)"));
        ExpectInvalid(Resolve({}, R"(<root control=""/>)"));
        ExpectInvalid(Resolve({}, R"(<root control="True"/>)"));
    }

    TEST(ControlSettingsTest, BadConfigIsAnErrorEvenWhenTheCommandLineWouldOverrideIt) {
        ExpectInvalid(Resolve({"--control"}, R"(<root control="maybe"/>)"));
    }

    TEST(ControlSettingsTest, PortOnTheCommandLineWithoutTheApiIsAnError) {
        ExpectInvalid(Resolve({"--control-port=9001"}));
        ExpectInvalid(Resolve({"--control-port=9001"}, R"(<root control="false"/>)"));
    }

    TEST(ControlSettingsTest, PortOnTheCommandLineWithTheApiEnabledByTheConfigIsFine) {
        const auto settings = Resolve({"--control-port=9001"}, R"(<root control="true"/>)");
        ASSERT_TRUE(settings.has_value());
        EXPECT_TRUE(settings->enabled);
        EXPECT_EQ(settings->port, 9001);
    }

    TEST(ControlSettingsTest, PortFlagWithoutAValueIsAnError) {
        ExpectInvalid(Resolve({"--control", "--control-port"}));
    }

    TEST(ControlSettingsTest, UnknownArgumentsAreIgnored) {
        const auto settings = Resolve({"--fullscreen", "--control", "level=3", "--controls", "--control=1", "--control-ports=9"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_TRUE(settings->enabled);
        EXPECT_EQ(settings->port, kDefaultControlPort);
    }

    TEST(ControlSettingsTest, UnknownArgumentsAloneLeaveTheApiOff) {
        const auto settings = Resolve({"--fullscreen", "--controls"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_FALSE(settings->enabled);
    }

    TEST(ControlSettingsTest, ManualClockIsOffByDefault) {
        const auto settings = Resolve({"--control"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_FALSE(settings->manualClock);
    }

    TEST(ControlSettingsTest, CommandLineTurnsTheManualClockOn) {
        const auto settings = Resolve({"--control", "--manual-clock"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_TRUE(settings->enabled);
        EXPECT_TRUE(settings->manualClock);
    }

    TEST(ControlSettingsTest, ManualClockCanComeFromTheConfig) {
        const auto settings = Resolve({}, R"(<root control="true" manualClock="true"/>)");
        ASSERT_TRUE(settings.has_value());
        EXPECT_TRUE(settings->enabled);
        EXPECT_TRUE(settings->manualClock);

        const auto off = Resolve({}, R"(<root control="true" manualClock="0"/>)");
        ASSERT_TRUE(off.has_value());
        EXPECT_FALSE(off->manualClock);
    }

    TEST(ControlSettingsTest, ManualClockOnTheCommandLineBeatsTheConfig) {
        const auto settings = Resolve({"--manual-clock"}, R"(<root control="true" manualClock="false"/>)");
        ASSERT_TRUE(settings.has_value());
        EXPECT_TRUE(settings->manualClock);
    }

    TEST(ControlSettingsTest, ManualClockWithoutTheApiIsAnError) {
        // Nobody could step the game: it would freeze
        ExpectInvalid(Resolve({"--manual-clock"}));
        ExpectInvalid(Resolve({}, R"(<root manualClock="true"/>)"));
        ExpectInvalid(Resolve({"--manual-clock"}, R"(<root control="false"/>)"));
    }

    TEST(ControlSettingsTest, ManualClockSwitchedOffWithoutTheApiIsFine) {
        const auto settings = Resolve({}, R"(<root manualClock="false"/>)");
        ASSERT_TRUE(settings.has_value());
        EXPECT_FALSE(settings->manualClock);
    }

    TEST(ControlSettingsTest, ManualClockAttributeThatIsNotABooleanIsAnError) {
        ExpectInvalid(Resolve({"--control"}, R"(<root manualClock="maybe"/>)"));
        ExpectInvalid(Resolve({"--control"}, R"(<root manualClock=""/>)"));
    }

    TEST(ControlSettingsTest, ManualClockFlagLookalikesAreIgnored) {
        const auto settings = Resolve({"--control", "--manual-clock=1", "--manual-clocks", "manual-clock"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_FALSE(settings->manualClock);
    }

    TEST(ControlSettingsTest, HeadlessIsOffByDefault) {
        const auto settings = Resolve({"--control"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_FALSE(settings->headless);
    }

    TEST(ControlSettingsTest, CommandLineTurnsHeadlessOn) {
        const auto settings = Resolve({"--control", "--headless"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_TRUE(settings->headless);
        EXPECT_FALSE(settings->manualClock);
    }

    TEST(ControlSettingsTest, HeadlessAndTheManualClockAreIndependent) {
        const auto both = Resolve({"--control", "--headless", "--manual-clock"});
        ASSERT_TRUE(both.has_value());
        EXPECT_TRUE(both->headless);
        EXPECT_TRUE(both->manualClock);

        const auto clockOnly = Resolve({"--control", "--manual-clock"});
        ASSERT_TRUE(clockOnly.has_value());
        EXPECT_FALSE(clockOnly->headless);
    }

    TEST(ControlSettingsTest, HeadlessCanComeFromTheConfigAndTheCommandLineBeatsIt) {
        const auto fromConfig = Resolve({}, R"(<root control="true" headless="true"/>)");
        ASSERT_TRUE(fromConfig.has_value());
        EXPECT_TRUE(fromConfig->headless);

        const auto off = Resolve({}, R"(<root control="true" headless="0"/>)");
        ASSERT_TRUE(off.has_value());
        EXPECT_FALSE(off->headless);

        const auto flagBeatsConfig = Resolve({"--headless"}, R"(<root control="true" headless="false"/>)");
        ASSERT_TRUE(flagBeatsConfig.has_value());
        EXPECT_TRUE(flagBeatsConfig->headless);
    }

    TEST(ControlSettingsTest, HeadlessWithoutTheApiIsAnError) {
        // A game without a window that nobody can reach would run unseen
        ExpectInvalid(Resolve({"--headless"}));
        ExpectInvalid(Resolve({}, R"(<root headless="true"/>)"));
        ExpectInvalid(Resolve({"--headless"}, R"(<root control="false"/>)"));
    }

    TEST(ControlSettingsTest, HeadlessSwitchedOffWithoutTheApiIsFine) {
        const auto settings = Resolve({}, R"(<root headless="false"/>)");
        ASSERT_TRUE(settings.has_value());
        EXPECT_FALSE(settings->headless);
    }

    TEST(ControlSettingsTest, HeadlessAttributeThatIsNotABooleanIsAnError) {
        ExpectInvalid(Resolve({"--control"}, R"(<root headless="yes please"/>)"));
        ExpectInvalid(Resolve({"--control"}, R"(<root headless=""/>)"));
    }

    TEST(ControlSettingsTest, HeadlessFlagLookalikesAreIgnored) {
        const auto settings = Resolve({"--control", "--headless=1", "--headlessly", "headless"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_FALSE(settings->headless);
    }

    TEST(ControlSettingsTest, WindowAndDensityAreOffByDefault) {
        const auto settings = Resolve({"--control", "--headless"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_FALSE(settings->windowSize.has_value());
        EXPECT_FALSE(settings->density.has_value());
    }

    TEST(ControlSettingsTest, WindowAndDensityFlagsAreRead) {
        const auto settings = Resolve({"--control", "--headless", "--window=480x320", "--density=2"});
        ASSERT_TRUE(settings.has_value());
        ASSERT_TRUE(settings->windowSize.has_value());
        EXPECT_EQ(settings->windowSize->width, 480);
        EXPECT_EQ(settings->windowSize->height, 320);
        ASSERT_TRUE(settings->density.has_value());
        EXPECT_FLOAT_EQ(*settings->density, 2.0f);
    }

    TEST(ControlSettingsTest, EachWindowFlagCanComeAlone) {
        const auto windowOnly = Resolve({"--control", "--headless", "--window=640x480"});
        ASSERT_TRUE(windowOnly.has_value());
        EXPECT_TRUE(windowOnly->windowSize.has_value());
        EXPECT_FALSE(windowOnly->density.has_value());

        const auto densityOnly = Resolve({"--control", "--headless", "--density=1.5"});
        ASSERT_TRUE(densityOnly.has_value());
        EXPECT_FALSE(densityOnly->windowSize.has_value());
        EXPECT_FLOAT_EQ(*densityOnly->density, 1.5f);
    }

    TEST(ControlSettingsTest, HeadlessCanComeFromTheConfigForTheWindowFlags) {
        const auto settings = Resolve({"--window=480x480"}, R"(<root control="true" headless="true"/>)");
        ASSERT_TRUE(settings.has_value());
        EXPECT_EQ(settings->windowSize->width, 480);
    }

    TEST(ControlSettingsTest, TheLastWindowFlagWins) {
        const auto settings = Resolve({"--control", "--headless", "--window=100x100", "--window=200x300"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_EQ(settings->windowSize->width, 200);
        EXPECT_EQ(settings->windowSize->height, 300);
    }

    TEST(ControlSettingsTest, WindowFlagsWithoutHeadlessAreAnError) {
        ExpectInvalid(Resolve({"--control", "--window=480x480"}));
        ExpectInvalid(Resolve({"--control", "--density=2"}));
        ExpectInvalid(Resolve({"--control", "--window=480x480", "--density=2"}));
        ExpectInvalid(Resolve({"--control", "--window=480x480"}, R"(<root headless="false"/>)"));
    }

    TEST(ControlSettingsTest, WindowFlagsWithoutTheApiAreAnError) {
        ExpectInvalid(Resolve({"--headless", "--window=480x480"}));
    }

    TEST(ControlSettingsTest, ABareWindowOrDensityFlagNeedsAValue) {
        ExpectInvalid(Resolve({"--control", "--headless", "--window"}));
        ExpectInvalid(Resolve({"--control", "--headless", "--density"}));
        ExpectInvalid(Resolve({"--control", "--headless", "--window="}));
        ExpectInvalid(Resolve({"--control", "--headless", "--density="}));
    }

    TEST(ControlSettingsTest, AWindowThatIsNotTwoIntegersIsAnError) {
        for (const char* value : {"--window=480", "--window=480x", "--window=x480", "--window=480X480", "--window=480*480", "--window=480x480x2", "--window=4.5x480", "--window=480x 480",
                                  "--window= 480x480", "--window=+480x480", "--window=abcxdef", "--window=0x0"}) {
            SCOPED_TRACE(value);
            ExpectInvalid(Resolve({"--control", "--headless", value}));
        }
    }

    TEST(ControlSettingsTest, ADensityThatIsNotANumberIsAnError) {
        for (const char* value : {"--density=fast", "--density=2x", "--density= 2", "--density=+2", "--density=nan", "--density=inf", "--density=-1", "--density=0"}) {
            SCOPED_TRACE(value);
            ExpectInvalid(Resolve({"--control", "--headless", value}));
        }
    }

    TEST(ControlSettingsTest, TheWindowLimitsAreCheckedAtTheirEdge) {
        const auto accepts = [](const char* flag) { return Resolve({"--control", "--headless", flag}).has_value(); };

        EXPECT_TRUE(accepts("--window=64x64"));
        EXPECT_FALSE(accepts("--window=63x64"));
        EXPECT_FALSE(accepts("--window=64x63"));
        EXPECT_TRUE(accepts("--window=4096x4096"));
        EXPECT_FALSE(accepts("--window=4097x64"));
        EXPECT_FALSE(accepts("--window=64x4097"));
        EXPECT_TRUE(accepts("--density=0.5"));
        EXPECT_FALSE(accepts("--density=0.49"));
        EXPECT_TRUE(accepts("--density=4"));
        EXPECT_FALSE(accepts("--density=4.01"));
    }

    TEST(ControlSettingsTest, TheOutputSideIsCheckedForTheTwoFlagsTogether) {
        const auto resolves = [](const char* density) { return Resolve({"--control", "--headless", "--window=4096x4096", density}).has_value(); };

        EXPECT_TRUE(resolves("--density=2"));
        EXPECT_FALSE(resolves("--density=2.01"));
    }

    TEST(ControlSettingsTest, TheMessageNamesTheFlagAtFault) {
        const auto tooSmall = Resolve({"--control", "--headless", "--window=10x500"});
        ASSERT_FALSE(tooSmall.has_value());
        EXPECT_NE(tooSmall.error().message.find("--window="), String::npos) << tooSmall.error().message.c_str();
        EXPECT_NE(tooSmall.error().message.find("width"), String::npos) << tooSmall.error().message.c_str();

        const auto tooDense = Resolve({"--control", "--headless", "--density=9"});
        ASSERT_FALSE(tooDense.has_value());
        EXPECT_NE(tooDense.error().message.find("--density="), String::npos) << tooDense.error().message.c_str();
    }

    TEST(ControlSettingsTest, WindowFlagLookalikesAreIgnored) {
        const auto settings = Resolve({"--control", "--headless", "--windows=480x480", "--windowed", "--density-scale=2", "window=480x480"});
        ASSERT_TRUE(settings.has_value());
        EXPECT_FALSE(settings->windowSize.has_value());
        EXPECT_FALSE(settings->density.has_value());
    }

}  // namespace BECore::Tests
