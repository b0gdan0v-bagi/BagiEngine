#include <BECore/Control/ControlSettings.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using Args = eastl::vector<eastl::string>;

        /// The <root> element of @p xml, the shape ConfigManager::GetConfig() hands out. @p config keeps the document alive.
        XmlNode ParseRoot(const XmlConfig& config, const char* xml) {
            EXPECT_TRUE(config.LoadFromString(xml)) << xml;
            return config.GetRoot();
        }

        std::expected<ControlSettings, ControlError> Resolve(const Args& args, const char* configXml = nullptr) {
            if (configXml == nullptr) {
                return ResolveControlSettings(args, XmlNode{pugi::xml_node{}});
            }
            const XmlConfig config = XmlConfig::Create();
            return ResolveControlSettings(args, ParseRoot(config, configXml));
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
            const eastl::string arg = Format("--control-port={}", value);
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

}  // namespace BECore::Tests
