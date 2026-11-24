#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Common/Wait.h>
#include <Widgets/ArtGen/ArtGenLaunch.h>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    TEST(ArtGenLaunchTest, TheBridgeIsFoundInTheFirstRootThatHasIt) {
        const Path base = TestTempDirectory("artgen_launch");
        const Path engine = base / "engine";
        const Path game = base / "game";
        MakeDirectory(engine / "config");
        MakeDirectory(game / "config");
        Vector<Path> layers{engine / "config", game / "config"};
        EXPECT_FALSE(FindArtGenBridge(layers).has_value());

        const Path script = engine / "tools" / "artgen" / "artgen_bridge.py";
        WriteFile(script, "");
        const auto found = FindArtGenBridge(layers);
        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(*found, script);
        RemoveDirectory(base);
    }

    TEST(ArtGenLaunchTest, TheBridgeIsFoundUnderANonAsciiRootAndAFolderOfThatNameIsNoScript) {
        const Path base = TestTempDirectory("artgen_launch_utf8");
        RemoveDirectory(base);
        const Path engine = base / "caf\xC3\xA9";
        const Path game = base / "game";
        MakeDirectory(engine / "config");
        MakeDirectory(game / "config");
        MakeDirectory(game / "tools" / "artgen" / "artgen_bridge.py");
        const Vector<Path> layers{game / "config", engine / "config"};
        EXPECT_FALSE(FindArtGenBridge(layers).has_value());

        const Path script = engine / "tools" / "artgen" / "artgen_bridge.py";
        WriteFile(script, "");
        const auto found = FindArtGenBridge(layers);
        ASSERT_TRUE(found.has_value());
        EXPECT_EQ(*found, script);
        RemoveDirectory(base);
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
        const Path base = TestTempDirectory("artgen_launch_run");
        MakeDirectory(base);
        const Path script = base / "artgen_bridge.py";
        const Path log = base / "bridge.log";
        WriteFile(script, "import sys\nopen(sys.argv[sys.argv.index('--log') + 1], 'w').write(' '.join(sys.argv[1:]))\n");

        String error;
        if (!LaunchArtGenBridge(script, 8765, log, error)) {
            GTEST_SKIP() << "no " << ArtGenPython().c_str() << " to run: " << error.c_str();
        }
        ASSERT_TRUE(WaitUntil([&] { return PathExists(log); })) << "the stand-in bridge did not run in " << kWaitLimit.count() << " s";
        String written;
        WaitUntil([&] {
            written = ReadFile(log);
            return written.find("--exit-with-editor") != String::npos;
        });
        EXPECT_NE(written.find("--port 8765 --exit-with-editor --log"), String::npos) << written.c_str();
        RemoveDirectory(base);
    }

}  // namespace BECore::Tests
