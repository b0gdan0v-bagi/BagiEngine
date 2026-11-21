#include <BECore/FileSystem/FileWatchSetting.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    TEST(FileWatchSettingTest, AutoWatchesInTheEditorAndWhereAssertsAreOn) {
        EXPECT_TRUE(ShouldWatchFiles(FileWatchSetting::Auto, RunMode::Editor, false, false));
        EXPECT_TRUE(ShouldWatchFiles(FileWatchSetting::Auto, RunMode::Editor, true, false));
        EXPECT_TRUE(ShouldWatchFiles(FileWatchSetting::Auto, RunMode::Game, true, false));
        EXPECT_FALSE(ShouldWatchFiles(FileWatchSetting::Auto, RunMode::Game, false, false));
    }

    TEST(FileWatchSettingTest, OnAndOffIgnoreTheModeAndTheAsserts) {
        for (const RunMode mode : {RunMode::Editor, RunMode::Game}) {
            for (const bool assertsOn : {false, true}) {
                EXPECT_TRUE(ShouldWatchFiles(FileWatchSetting::On, mode, assertsOn, false));
                EXPECT_FALSE(ShouldWatchFiles(FileWatchSetting::Off, mode, assertsOn, false));
            }
        }
    }

    TEST(FileWatchSettingTest, TheWebNeverWatches) {
        for (const FileWatchSetting setting : {FileWatchSetting::Auto, FileWatchSetting::On, FileWatchSetting::Off}) {
            EXPECT_FALSE(ShouldWatchFiles(setting, RunMode::Editor, true, true));
        }
    }

}  // namespace BECore::Tests
