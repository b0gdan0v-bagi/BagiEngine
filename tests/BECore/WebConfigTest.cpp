#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Logger/ConsoleSink.h>
#include <BECore/Logger/FileSink.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Logger/OutputSink.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/Printers.h>
#include <CoreSDL/SDLUtils.h>
#include <CoreSDL/SDLWebCanvas.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// web/config/<fileName> of this engine checkout, found by walking up from the working directory (inside the repository, like FileSystem does for config/)
        std::filesystem::path WebConfigPath(const char* fileName) {
            std::error_code error;
            for (std::filesystem::path dir = std::filesystem::current_path(error); !dir.empty(); dir = dir.parent_path()) {
                const std::filesystem::path candidate = dir / "web" / "config" / fileName;
                if (std::filesystem::exists(candidate, error)) {
                    return candidate;
                }
                if (dir == dir.parent_path()) {
                    break;
                }
            }
            return {};
        }

        /// The first child of the root of an XML file with the given name, or an empty node
        pugi::xml_node FirstChildOfRoot(pugi::xml_document& document, const char* fileName, const char* childName) {
            const std::filesystem::path path = WebConfigPath(fileName);
            EXPECT_FALSE(path.empty()) << fileName << " is not under web/config";
            EXPECT_TRUE(document.load_file(path.c_str())) << path;
            return document.child("root").child(childName);
        }

    }  // namespace

    TEST(WebConfigTest, TheThreeFilesExistAndParse) {
        for (const char* fileName : {"ApplicationConfig.xml", "SDLWindowConfig.xml", "LoggerConfig.xml"}) {
            SCOPED_TRACE(fileName);
            const std::filesystem::path path = WebConfigPath(fileName);
            ASSERT_FALSE(path.empty());
            pugi::xml_document document;
            const pugi::xml_parse_result result = document.load_file(path.c_str());
            EXPECT_TRUE(result) << result.description();
            EXPECT_TRUE(document.child("root"));
        }
    }

    TEST(WebConfigTest, ApplicationConfigAsksForTheSdlRendererInGameMode) {
        const std::filesystem::path path = WebConfigPath("ApplicationConfig.xml");
        ASSERT_FALSE(path.empty());
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromFile(path));

        const ApplicationConfigLoad load = LoadApplicationConfig(deserializer);
        EXPECT_TRUE(load.errors.empty());
        EXPECT_EQ(load.config._type, "SDL3ApplicationFabric");
        EXPECT_EQ(load.config._renderer, "SDLRendererBackend");
        EXPECT_EQ(load.config._path, "config/SDLWindowConfig.xml");
        EXPECT_EQ(load.config._mode, RunMode::Game);
        // The control API has no socket in the browser
        EXPECT_FALSE(load.config._control);
    }

    TEST(WebConfigTest, WindowIsResizableAndHasNoVulkanFlag) {
        pugi::xml_document document;
        const pugi::xml_node window = FirstChildOfRoot(document, "SDLWindowConfig.xml", "window");
        ASSERT_TRUE(window);

        const char* flagsText = window.attribute("windowFlags").as_string();
        const SDL_WindowFlags flags = SDLUtils::ParseWindowFlags(flagsText);
        EXPECT_TRUE((flags & SDL_WINDOW_RESIZABLE) != 0) << flagsText;
        EXPECT_TRUE((flags & SDL_WINDOW_HIGH_PIXEL_DENSITY) != 0) << flagsText;
        EXPECT_TRUE((flags & SDL_WINDOW_VULKAN) == 0) << flagsText;
        EXPECT_EQ(StringView(flagsText).find("SDL_WINDOW_VULKAN"), StringView::npos);
        EXPECT_GT(window.attribute("width").as_int(), 0);
        EXPECT_GT(window.attribute("height").as_int(), 0);
    }

    TEST(WebConfigTest, LoggerWritesToTheConsoleOnlyWithoutColors) {
        const std::filesystem::path path = WebConfigPath("LoggerConfig.xml");
        ASSERT_FALSE(path.empty());
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromFile(path));

        // The sinks as the LoggerManager builds them: a disabled entry is dropped
        const auto manager = New<LoggerManager>();
        manager->Deserialize(deserializer);
        EXPECT_FALSE(deserializer.HasErrors());
        EXPECT_EQ(manager->GetSinks().size(), 1u);
        EXPECT_NE(manager->GetSink<ConsoleSink>(), nullptr);
        EXPECT_EQ(manager->GetSink<FileSink>(), nullptr);
        EXPECT_EQ(manager->GetSink<OutputSink>(), nullptr);

        // colorEnabled is private to the sink: read it from the file
        pugi::xml_document document;
        bool consoleChecked = false;
        for (const pugi::xml_node sink : FirstChildOfRoot(document, "LoggerConfig.xml", "sinks").children("sink")) {
            const StringView type = sink.attribute("type").as_string();
            if (type == "ConsoleSink") {
                EXPECT_TRUE(sink.attribute("enabled").as_bool(true));
                EXPECT_TRUE(sink.attribute("colorEnabled")) << "colorEnabled must be spelled out";
                EXPECT_FALSE(sink.attribute("colorEnabled").as_bool(true));
                consoleChecked = true;
            } else if (type == "FileSink") {
                EXPECT_FALSE(sink.attribute("enabled").as_bool(true));
            }
        }
        EXPECT_TRUE(consoleChecked);
    }

    TEST(WebCanvasTest, TheWindowTakesTheInnerSizeOfThePage) {
        const WebCanvasSize size = FitWindowSize(1280.0, 720.0, 64, 64);
        EXPECT_EQ(size.width, 1280);
        EXPECT_EQ(size.height, 720);
    }

    TEST(WebCanvasTest, ASizeBetweenWholeUnitsIsRounded) {
        // A page zoomed to 110 % reports fractions of a CSS pixel
        const WebCanvasSize size = FitWindowSize(1023.6, 767.4, 64, 64);
        EXPECT_EQ(size.width, 1024);
        EXPECT_EQ(size.height, 767);
    }

    TEST(WebCanvasTest, NeverBelowTheMinimum) {
        // A page that is not laid out yet (or minimised) reports 0
        const WebCanvasSize nothing = FitWindowSize(0.0, 0.0, 64, 48);
        EXPECT_EQ(nothing.width, 64);
        EXPECT_EQ(nothing.height, 48);

        const WebCanvasSize narrow = FitWindowSize(10.0, 700.0, 64, 48);
        EXPECT_EQ(narrow.width, 64);
        EXPECT_EQ(narrow.height, 700);
    }

    TEST(WebCanvasTest, AnAxisIsFittedOnItsOwn) {
        const WebCanvasSize size = FitWindowSize(300.0, 20.0, 64, 48);
        EXPECT_EQ(size.width, 300);
        EXPECT_EQ(size.height, 48);
    }

    TEST(WebCanvasTest, ANumberThatIsNotASizeCountsAsZero) {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double infinity = std::numeric_limits<double>::infinity();
        for (const double bad : {nan, infinity, -infinity, -5.0}) {
            SCOPED_TRACE(ToString(bad));
            const WebCanvasSize size = FitWindowSize(bad, bad, 64, 48);
            EXPECT_EQ(size.width, 64);
            EXPECT_EQ(size.height, 48);
        }
    }

    TEST(WebCanvasTest, AHugeValueDoesNotOverflow) {
        const WebCanvasSize size = FitWindowSize(1.0e18, 1.0e18, 64, 64);
        EXPECT_GT(size.width, 0);
        EXPECT_GT(size.height, 0);
    }

    TEST(WebCanvasTest, WithoutInsetsTheSafeAreaIsTheWholeWindow) {
        const WebSafeArea area = SafeAreaFromInsets(1280.0, 720.0, 0.0, 0.0, 0.0, 0.0);
        EXPECT_EQ(area.x, 0.0);
        EXPECT_EQ(area.y, 0.0);
        EXPECT_EQ(area.width, 1280.0);
        EXPECT_EQ(area.height, 720.0);
    }

    TEST(WebCanvasTest, ANotchTakesItsSideOfTheSafeArea) {
        // A phone in landscape: the notch on the left, the home bar below
        const WebSafeArea area = SafeAreaFromInsets(844.0, 390.0, 0.0, 47.0, 21.0, 47.0);
        EXPECT_EQ(area.x, 47.0);
        EXPECT_EQ(area.y, 0.0);
        EXPECT_EQ(area.width, 844.0 - 47.0 - 47.0);
        EXPECT_EQ(area.height, 390.0 - 21.0);
    }

    TEST(WebCanvasTest, AnInsetThatIsNotANumberCountsAsNone) {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double infinity = std::numeric_limits<double>::infinity();
        for (const double bad : {nan, infinity, -infinity, -12.0}) {
            SCOPED_TRACE(ToString(bad));
            const WebSafeArea area = SafeAreaFromInsets(800.0, 600.0, bad, bad, bad, 10.0);
            EXPECT_EQ(area.x, 10.0);
            EXPECT_EQ(area.y, 0.0);
            EXPECT_EQ(area.width, 790.0);
            EXPECT_EQ(area.height, 600.0);
        }
    }

    TEST(WebCanvasTest, InsetsLargerThanTheWindowLeaveTheWholeWindow) {
        // Both sides together leave nothing: the area is never empty
        const WebSafeArea area = SafeAreaFromInsets(400.0, 300.0, 0.0, 250.0, 0.0, 250.0);
        EXPECT_EQ(area.x, 0.0);
        EXPECT_EQ(area.y, 0.0);
        EXPECT_EQ(area.width, 400.0);
        EXPECT_EQ(area.height, 300.0);

        const WebSafeArea wild = SafeAreaFromInsets(400.0, 300.0, 1.0e18, 1.0e18, 1.0e18, 1.0e18);
        EXPECT_EQ(wild.width, 400.0);
        EXPECT_EQ(wild.height, 300.0);
    }

    TEST(WebCanvasTest, OneInsetBeyondTheWindowGivesTheWholeWindow) {
        // The right inset alone covers the whole width: nothing is left, so the whole window answers
        const WebSafeArea area = SafeAreaFromInsets(400.0, 300.0, 20.0, 9999.0, 0.0, 0.0);
        EXPECT_EQ(area.width, 400.0);
        EXPECT_EQ(area.height, 300.0);
    }

    TEST(WebCanvasTest, AWindowWithoutSizeHasNoSafeAreaToCut) {
        const WebSafeArea area = SafeAreaFromInsets(0.0, 0.0, 10.0, 10.0, 10.0, 10.0);
        EXPECT_EQ(area.width, 0.0);
        EXPECT_EQ(area.height, 0.0);
    }

}  // namespace BECore::Tests
