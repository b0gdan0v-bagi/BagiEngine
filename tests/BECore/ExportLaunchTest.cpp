#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Common/Wait.h>
#include <Widgets/EditorLayout/EngineTools.h>
#include <Widgets/Export/ExportLaunch.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// Short, so a stand-in that ignores its cancel does not hold the test for the editor's grace
        constexpr uint64_t kShortGraceMs = 200;

        /// Runs the stand-in script @p text with the arguments @p extra; skips the test when there is no Python
        class ExportProcessTest : public ::testing::Test {
        protected:
            UniquePtr<IExportProcess> Start(const char* text, uint64_t killGraceMs, String& error) {
                const String script = _folder.Write("stand_in.py", text);
                _launcher = MakeExportLauncher(killGraceMs);
                const Vector<String> arguments{EnginePython(), script, _folder.PathOf("marker")};
                auto started = _launcher->Start({arguments.data(), arguments.size()}, _folder.PathOf("export.log"));
                if (!started) {
                    error = started.error();
                    return nullptr;
                }
                return std::move(*started);
            }

            String Log() const {
                return ReadFile(Path(_folder.PathOf("export.log")));
            }

            bool WaitForLog(StringView part) const {
                return WaitUntil([&] { return Log().find(part.data(), 0, part.size()) != String::npos; });
            }

            TestTempFolder _folder{"export_process"};
            UniquePtr<IExportLauncher> _launcher;
        };

        /// Says it is ready, waits for its input to end and leaves with what it was told
        constexpr const char* kObedient = "import sys\n"
                                          "print('ready', flush=True)\n"
                                          "print('to stderr', file=sys.stderr, flush=True)\n"
                                          "told = sys.stdin.readline().strip()\n"
                                          "open(sys.argv[1], 'w').write(told)\n"
                                          "sys.exit(3 if told == 'cancel' else 1)\n";

    }  // namespace

    TEST(ExportLaunchTest, APathWithSpacesIsOneArgumentAndAnEmptyFolderIsLeftToTheScript) {
        const Vector<String> arguments =
            BuildExportLaunch("python3", "/the engine/tools/export/export.py", {.executable = "/my game/bin/Game", .config = ExportConfig::Release, .outputDirectory = "/out folder"});
        const Vector<String> expected{"python3", "/the engine/tools/export/export.py", "build", "--exe", "/my game/bin/Game", "--config", "release", "--out", "/out folder", "--cancel-on-stdin-close"};
        EXPECT_EQ(arguments, expected);

        const Vector<String> byDefault = BuildExportLaunch("python3", "/e/export.py", {.executable = "/g/Game"});
        const Vector<String> expectedByDefault{"python3", "/e/export.py", "build", "--exe", "/g/Game", "--config", "debug", "--cancel-on-stdin-close"};
        EXPECT_EQ(byDefault, expectedByDefault);
    }

    TEST(ExportLaunchTest, EachSceneLeftOutIsAnArgumentOfItsOwn) {
        const Vector<String> excluded{"Main Menu", "Credits"};
        const Vector<String> arguments = BuildExportLaunch("python3", "/e/export.py", {.executable = "/g/Game", .excludedScenes = {excluded.data(), excluded.size()}});
        const Vector<String> expected{"python3", "/e/export.py",    "build",     "--exe",           "/g/Game", "--config",
                                      "debug",   "--exclude-scene", "Main Menu", "--exclude-scene", "Credits", "--cancel-on-stdin-close"};
        EXPECT_EQ(arguments, expected);
    }

    TEST(ExportLaunchTest, RunNamesThePackageFolder) {
        const Vector<String> expected{"python3", "/e/export.py", "run", "--dir", "/my game/dist"};
        EXPECT_EQ(BuildPackageRunLaunch("python3", "/e/export.py", "/my game/dist"), expected);
    }

    TEST(ExportLaunchTest, TheExecutableIsMadeAbsoluteAndAPathThatIsNoFileIsRefused) {
        const TestTempFolder folder("export_exe");
        const String program = folder.Write("Game", "x");

        const auto found = ResolveExportExecutable(program);
        ASSERT_TRUE(found.has_value()) << found.error().c_str();
        EXPECT_EQ(*found, program);

        EXPECT_FALSE(ResolveExportExecutable("").has_value());
        EXPECT_FALSE(ResolveExportExecutable(folder.PathOf("Missing")).has_value());
        EXPECT_FALSE(ResolveExportExecutable(folder.PathOf(".")).has_value());
        // A bare name found on PATH is no file of the working folder
        const auto bare = ResolveExportExecutable("no_such_program_of_the_export_test");
        ASSERT_FALSE(bare.has_value());
        EXPECT_NE(bare.error().find("no_such_program_of_the_export_test"), String::npos) << bare.error().c_str();
    }

    TEST_F(ExportProcessTest, BothStreamsReachTheLogAndACancelIsToldOnTheInput) {
        String error;
        const UniquePtr<IExportProcess> process = Start(kObedient, kExportKillGraceMs, error);
        if (!process) {
            GTEST_SKIP() << "no " << EnginePython().c_str() << " to run: " << error.c_str();
        }
        ASSERT_TRUE(WaitForLog("to stderr")) << Log().c_str();
        EXPECT_NE(Log().find("ready"), String::npos);
        EXPECT_FALSE(process->Poll().has_value());

        process->Cancel();
        Optional<int> code;
        ASSERT_TRUE(WaitUntil([&] { return (code = process->Poll()).has_value(); }));
        EXPECT_EQ(*code, kExportExitCancelled);
        EXPECT_EQ(ReadFile(Path(_folder.PathOf("marker"))), "cancel");
    }

    TEST_F(ExportProcessTest, DestroyingARunningProcessCancelsIt) {
        String error;
        UniquePtr<IExportProcess> process = Start(kObedient, kExportKillGraceMs, error);
        if (!process) {
            GTEST_SKIP() << "no " << EnginePython().c_str() << " to run: " << error.c_str();
        }
        ASSERT_TRUE(WaitForLog("ready")) << Log().c_str();
        process.reset();
        const Path marker(_folder.PathOf("marker"));
        EXPECT_TRUE(WaitUntil([&] { return PathExists(marker) && ReadFile(marker) == "cancel"; }));
    }

    TEST_F(ExportProcessTest, AProcessThatIgnoresItsCancelIsKilledAfterTheGrace) {
        String error;
        const UniquePtr<IExportProcess> process = Start("import time\nprint('ready', flush=True)\ntime.sleep(600)\n", kShortGraceMs, error);
        if (!process) {
            GTEST_SKIP() << "no " << EnginePython().c_str() << " to run: " << error.c_str();
        }
        ASSERT_TRUE(WaitForLog("ready")) << Log().c_str();
        process->Cancel();
        Optional<int> code;
        ASSERT_TRUE(WaitUntil([&] { return (code = process->Poll()).has_value(); }));
        EXPECT_NE(*code, 0);
    }

}  // namespace BECore::Tests
