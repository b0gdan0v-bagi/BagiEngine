#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Assert/AssertHandlers.h>
#include <BECore/Audio/AudioSettings.h>
#include <BECore/Config/ContentVersion.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Tests/TestList.h>
#include <BECore/Widgets/WidgetList.h>
#include <Common/ConfigFilesCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <CoreSDL/SDLWindowConfig.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kWindowElement = "window";
        constexpr StringView kUIStringsReason = "read by the string table (UIStrings::Initialize), its keys are the data";
        // WidgetList reads them, but the widget classes are registered by the runtime and editor executables: the test executable's factory cannot make them
        constexpr StringView kWidgetsReason = "a WidgetList of widgets that only the application executable registers";

        std::filesystem::path EngineRoot() {
            return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        }

        ConfigReader ApplicationConfigReader() {
            return {"ApplicationConfig", [](IDeserializer& d) { ApplicationConfig{}.Deserialize(d); }};
        }

        ConfigReader SceneConfigReader() {
            return {"SceneConfig", [](IDeserializer& d) { New<SceneManager>()->Deserialize(d); }};
        }

        /// The reader of every config the engine ships
        Vector<ConfigReader> EngineReaders() {
            return {
                ApplicationConfigReader(),
                {"LoggerConfig", [](IDeserializer& d) { New<LoggerManager>()->Deserialize(d); }},
                {"AssertHandlersConfig", [](IDeserializer& d) { New<AssertHandlerManager>()->Deserialize(d); }},
                {"AudioConfig", [](IDeserializer& d) { AudioSettings{}.Deserialize(d); }},
                {"TestsConfig", [](IDeserializer& d) { New<Tests::TestList>()->Deserialize(d); }},
                {"SDLWindowConfig",
                 [](IDeserializer& d) {
                     if (d.BeginObject(kWindowElement)) {
                         SDLWindowConfig{}.Deserialize(d);
                         d.EndObject();
                     }
                 }},
                SceneConfigReader(),
            };
        }

        /// The files whose reader is not a reflected type
        Vector<HandReadConfig> EngineHandRead() {
            return {
                {"TextConfig", "read by hand: the font and text settings of the text system"},
                {"UIConfig", "read by hand: the UI environment"},
                {"WidgetsConfig", kWidgetsReason},
                {"GameWidgetsConfig", kWidgetsReason},
            };
        }

        std::filesystem::path SeededFolder() {
            const std::filesystem::path dir = TestTempDirectory("BagiConfigFilesCheck");
            std::filesystem::remove_all(dir);
            return dir;
        }

    }  // namespace

    class EngineConfigFilesTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }
    };

    TEST_F(EngineConfigFilesTest, TheEngineConfigsAreCleanUnderTheStrictMode) {
        const Vector<ConfigReader> readers = EngineReaders();
        const Vector<HandReadConfig> byHand = EngineHandRead();
        ExpectConfigFilesClean(EngineRoot() / "config", readers, byHand);
    }

    TEST_F(EngineConfigFilesTest, TheStressConfigsAreCleanUnderTheStrictMode) {
        const Vector<ConfigReader> readers{ApplicationConfigReader(), SceneConfigReader()};
        const Vector<HandReadConfig> byHand{{"UIStrings", kUIStringsReason}};
        ExpectConfigFilesClean(EngineRoot() / "tools" / "stress" / "config", readers, byHand);
    }

    /// The helper a game's own test calls: a folder with a typo, a missing required key and an unlisted file
    TEST_F(EngineConfigFilesTest, TheFinderNamesEveryKindOfProblem) {
        const std::filesystem::path dir = SeededFolder();
        WriteFile(dir / "ApplicationConfig.xml", "<root type=\"A\" rendrer=\"Vulkan\"/>");
        WriteFile(dir / "TestsConfig.xml", "<root><tests><test type=\"EngineSmokeTest\"/></tests></root>");
        WriteFile(dir / "Extra.xml", "<root/>");
        WriteFile(dir / "Hand.xml", "<root/>");

        const Vector<ConfigReader> readers{ApplicationConfigReader(), {"TestsConfig", [](IDeserializer& d) { New<Tests::TestList>()->Deserialize(d); }}, {"Ghost", [](IDeserializer&) {}}};
        const Vector<HandReadConfig> byHand{{"Hand", "by hand"}, {"AlsoGhost", "by hand"}};
        const Vector<String> messages = FindConfigFindings(dir, readers, byHand);
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);

        const auto has = [&](StringView text) {
            for (const String& message : messages) {
                if (StringView(message).find(text) != StringView::npos) {
                    return true;
                }
            }
            return false;
        };
        EXPECT_TRUE(has("ApplicationConfig.xml: rendrer: Unknown attribute 'rendrer', did you mean 'renderer'?")) << ::testing::PrintToString(messages);
        EXPECT_TRUE(has("Extra.xml: no reader and not on the list of files read by hand"));
        EXPECT_TRUE(has("Ghost.xml: a reader is listed but the file does not exist"));
        EXPECT_TRUE(has("AlsoGhost.xml: listed as read by hand (by hand) but the file does not exist"));
        EXPECT_EQ(messages.size(), 4u) << ::testing::PrintToString(messages);
    }

    TEST_F(EngineConfigFilesTest, TheFinderChecksTheContentOfTheSceneNodeFiles) {
        const std::filesystem::path dir = SeededFolder();
        WriteFile(
            dir / "SceneConfig.xml",
            Format("<SceneManager contentVersion=\"{}\"><scenes><scene type=\"Scene\" name=\"Main\"><nodes><node ref=\"nodes/A\"/></nodes></scene></scenes></SceneManager>", kContentVersion).c_str());
        WriteFile(dir / "nodes" / "A.xml", Format("<node name=\"A\" layerr=\"3\" contentVersion=\"{}\"/>", kContentVersion).c_str());

        const Vector<ConfigReader> readers{SceneConfigReader()};
        const Vector<String> messages = FindConfigFindings(dir, readers, {});
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);

        ASSERT_EQ(messages.size(), 1u) << ::testing::PrintToString(messages);
        EXPECT_NE(messages[0].find("SceneConfig.xml: scenes[0].nodes[0].layerr: Unknown attribute 'layerr'"), String::npos) << messages[0];
    }

    TEST_F(EngineConfigFilesTest, ACleanFolderHasNoFindings) {
        const std::filesystem::path dir = SeededFolder();
        WriteFile(dir / "ApplicationConfig.xml", "<root type=\"A\"/>");
        const Vector<ConfigReader> readers{ApplicationConfigReader()};
        EXPECT_TRUE(FindConfigFindings(dir, readers, {}).empty());
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }

}  // namespace BECore::Tests
