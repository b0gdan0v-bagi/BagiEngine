#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Widgets/ArtGen/ArtGenLaunch.h>
#include <cstdio>
#include <filesystem>
#include <gtest/gtest.h>

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
        const Vector<String> arguments = BuildBridgeLaunch("python3", "/e/tools/artgen/artgen_bridge.py", 8765);
        ASSERT_EQ(arguments.size(), 5u);
        EXPECT_EQ(arguments[0], "python3");
        EXPECT_EQ(arguments[1], "/e/tools/artgen/artgen_bridge.py");
        EXPECT_EQ(arguments[2], "--port");
        EXPECT_EQ(arguments[3], "8765");
        EXPECT_EQ(arguments[4], "--exit-with-editor");
        EXPECT_FALSE(ArtGenPython().empty());
    }

}  // namespace BECore::Tests
