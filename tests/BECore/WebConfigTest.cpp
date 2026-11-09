#include <BECore/Application/ApplicationConfig.h>
#include <BECore/Logger/ConsoleSink.h>
#include <BECore/Logger/FileSink.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Logger/OutputSink.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/Printers.h>
#include <CoreSDL/SDLUtils.h>
#include <filesystem>
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
        EXPECT_EQ(eastl::string_view(flagsText).find("SDL_WINDOW_VULKAN"), eastl::string_view::npos);
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
            const eastl::string_view type = sink.attribute("type").as_string();
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

}  // namespace BECore::Tests
