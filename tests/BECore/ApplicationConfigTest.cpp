#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        ApplicationConfigLoad Load(eastl::string_view xml) {
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(xml)) << xml.data();
            return LoadApplicationConfig(deserializer);
        }

        /// True when exactly one error was reported and it is under @p path
        bool HasOnlyErrorAt(const ApplicationConfigLoad& load, eastl::string_view path) {
            return load.errors.size() == 1 && load.errors[0].path.ToStringView() == path;
        }

    }  // namespace

    TEST(ApplicationConfig, ReadsEveryField) {
        const auto load = Load(R"(<root type="SDL3ApplicationFabric" renderer="SDLRendererBackend" path="config/Window.xml" mode="Game"
                                         control="true" controlPort="9100" manualClock="1" headless="true"/>)");
        EXPECT_TRUE(load.found);
        EXPECT_TRUE(load.errors.empty());
        EXPECT_EQ(load.config._type, "SDL3ApplicationFabric");
        EXPECT_EQ(load.config._renderer, "SDLRendererBackend");
        EXPECT_EQ(load.config._path, "config/Window.xml");
        EXPECT_EQ(load.config._mode, RunMode::Game);
        EXPECT_TRUE(load.config._control);
        EXPECT_EQ(load.config._controlPort, 9100);
        EXPECT_TRUE(load.config._manualClock);
        EXPECT_TRUE(load.config._headless);
    }

    TEST(ApplicationConfig, MissingAttributesGiveTheDefaults) {
        const auto load = Load(R"(<root/>)");
        EXPECT_TRUE(load.errors.empty());
        EXPECT_TRUE(load.config._type.empty());
        EXPECT_TRUE(load.config._renderer.empty());
        EXPECT_TRUE(load.config._path.empty());
        EXPECT_EQ(load.config._mode, RunMode::Editor);
        EXPECT_FALSE(load.config._control);
        EXPECT_EQ(load.config._controlPort, kDefaultControlPort);
        EXPECT_FALSE(load.config._manualClock);
        EXPECT_FALSE(load.config._headless);
    }

    TEST(ApplicationConfig, ReadsEveryMode) {
        EXPECT_EQ(Load(R"(<root mode="Editor"/>)").config._mode, RunMode::Editor);
        EXPECT_EQ(Load(R"(<root mode="Game"/>)").config._mode, RunMode::Game);
    }

    TEST(ApplicationConfig, UnknownModeKeepsEditorAndIsReported) {
        for (const char* xml : {R"(<root mode="game"/>)", R"(<root mode="Window"/>)", R"(<root mode=""/>)"}) {
            SCOPED_TRACE(xml);
            const auto load = Load(xml);
            EXPECT_EQ(load.config._mode, RunMode::Editor);
            ASSERT_TRUE(HasOnlyErrorAt(load, "mode"));
            EXPECT_NE(load.errors[0].errorMessage.ToStringView().find("Invalid enum value"), eastl::string_view::npos);
        }
    }

    TEST(ApplicationConfig, BadControlPortIsReportedUnderItsNameAndKeepsTheDefault) {
        for (const char* value : {"abc", "70000", "65536", "-1", ""}) {
            const eastl::string xml = Format("<root controlPort=\"{}\"/>", value);
            SCOPED_TRACE(xml.c_str());
            const auto load = Load(eastl::string_view(xml.data(), xml.size()));
            EXPECT_EQ(load.config._controlPort, kDefaultControlPort);
            EXPECT_TRUE(HasOnlyErrorAt(load, "controlPort"));
        }
    }

    TEST(ApplicationConfig, BadBooleanIsReportedUnderItsNameAndKeepsTheDefault) {
        for (const char* name : {"control", "manualClock", "headless"}) {
            for (const char* value : {"yes", "True", "2", ""}) {
                const eastl::string xml = Format("<root {}=\"{}\"/>", name, value);
                SCOPED_TRACE(xml.c_str());
                const auto load = Load(eastl::string_view(xml.data(), xml.size()));
                EXPECT_FALSE(load.config._control || load.config._manualClock || load.config._headless);
                EXPECT_TRUE(HasOnlyErrorAt(load, name));
            }
        }
    }

    TEST(ApplicationConfig, TheBooleanSpellingsOfTheOldReaderStillWork) {
        EXPECT_TRUE(Load(R"(<root control="1"/>)").config._control);
        EXPECT_FALSE(Load(R"(<root control="0"/>)").config._control);
        EXPECT_TRUE(Load(R"(<root headless="true"/>)").config._headless);
        EXPECT_EQ(Load(R"(<root controlPort="0"/>)").config._controlPort, 0);
        EXPECT_EQ(Load(R"(<root controlPort="65535"/>)").config._controlPort, 65535);
    }

    TEST(ApplicationConfig, AnUnknownAttributeIsIgnored) {
        const auto load = Load(R"(<root type="A" fullscreen="yes" level="3"/>)");
        EXPECT_TRUE(load.errors.empty());
        EXPECT_EQ(load.config._type, "A");
    }

    TEST(ApplicationConfig, LoadingSwitchesTheDeserializerToStrictValues) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(R"(<root control="true"/>)"));
        (void)LoadApplicationConfig(deserializer);
        EXPECT_TRUE(deserializer.IsStrictValues());
    }

    class ApplicationConfigRealFileTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }
    };

    TEST_F(ApplicationConfigRealFileTest, TheShippedConfigLoadsWithoutErrors) {
        const auto load = LoadApplicationConfig();
        EXPECT_TRUE(load.found);
        EXPECT_TRUE(load.errors.empty());
        EXPECT_EQ(load.config._type, "SDL3ApplicationFabric");
        EXPECT_FALSE(load.config._path.empty());
    }

}  // namespace BECore::Tests
