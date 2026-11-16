#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/ArtGen/ArtGenLaunch.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    TEST(ArtGenLaunchTest, TheBridgeIsFoundInTheFirstRootThatHasIt) {
        const std::filesystem::path base = TestTempDirectory("artgen_launch");
        const std::filesystem::path engine = base / "engine";
        const std::filesystem::path game = base / "game";
        std::filesystem::create_directories(engine / "config");
        std::filesystem::create_directories(game / "config");
        Vector<std::filesystem::path> layers{engine / "config", game / "config"};
        EXPECT_FALSE(FindArtGenBridge(layers).has_value());

        const std::filesystem::path script = engine / "tools" / "artgen" / "artgen_bridge.py";
        std::filesystem::create_directories(script.parent_path());
        std::FILE* file = std::fopen(script.string().c_str(), "wb");
        ASSERT_NE(file, nullptr);
        std::fclose(file);
        const auto found = FindArtGenBridge(layers);
        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(*found, script);
        std::filesystem::remove_all(base);
    }

    TEST(ArtGenLaunchTest, TheBridgeIsToldThePortAndToLeaveWithTheEditor) {
        const Vector<String> arguments = BuildBridgeLaunch("python3", "/e/tools/artgen/artgen_bridge.py", 8765, "/u/artgen-bridge.log");
        ASSERT_EQ(arguments.size(), 7u);
        EXPECT_EQ(arguments[0], "python3");
        EXPECT_EQ(arguments[1], "/e/tools/artgen/artgen_bridge.py");
        EXPECT_EQ(arguments[2], "--port");
        EXPECT_EQ(arguments[3], "8765");
        EXPECT_EQ(arguments[4], "--exit-with-editor");
        EXPECT_EQ(arguments[5], "--log");
        EXPECT_EQ(arguments[6], "/u/artgen-bridge.log");
        EXPECT_FALSE(ArtGenPython().empty());
    }

    // The real start: in the background, without the standard streams of the editor (one started from the desktop has none), and the script
    // gets its arguments. A stand-in script writes them to the log path it was given.
    TEST(ArtGenLaunchTest, TheBridgeStartsInTheBackgroundAndGetsItsArguments) {
        constexpr auto kWaitLimit = std::chrono::seconds(20);
        const std::filesystem::path base = TestTempDirectory("artgen_launch_run");
        std::filesystem::create_directories(base);
        const std::filesystem::path script = base / "artgen_bridge.py";
        const std::filesystem::path log = base / "bridge.log";
        std::FILE* file = std::fopen(script.string().c_str(), "wb");
        ASSERT_NE(file, nullptr);
        const char kStandIn[] = "import sys\nopen(sys.argv[sys.argv.index('--log') + 1], 'w').write(' '.join(sys.argv[1:]))\n";
        std::fwrite(kStandIn, 1, sizeof(kStandIn) - 1, file);
        std::fclose(file);

        String error;
        if (!LaunchArtGenBridge(script, 8765, log, error)) {
            GTEST_SKIP() << "no " << ArtGenPython().c_str() << " to run: " << error.c_str();
        }
        const auto deadline = std::chrono::steady_clock::now() + kWaitLimit;
        while (!std::filesystem::exists(log) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        ASSERT_TRUE(std::filesystem::exists(log)) << "the stand-in bridge did not run in " << kWaitLimit.count() << " s";
        String written;
        for (int attempt = 0; attempt < 100 && written.find("--exit-with-editor") == String::npos; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (std::FILE* read = std::fopen(log.string().c_str(), "rb")) {
                char buffer[256] = {};
                written = String(buffer, std::fread(buffer, 1, sizeof(buffer) - 1, read));
                std::fclose(read);
            }
        }
        EXPECT_NE(written.find("--port 8765 --exit-with-editor --log"), String::npos) << written.c_str();
        std::filesystem::remove_all(base);
    }

}  // namespace BECore::Tests
