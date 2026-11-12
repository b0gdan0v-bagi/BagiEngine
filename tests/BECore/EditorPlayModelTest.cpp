#include <Common/Printers.h>
#include <Widgets/EditorLayout/EditorPlayModel.h>

namespace BECore::Tests {

    TEST(EditorPlayModelTest, StartsStopped) {
        const EditorPlayModel model;
        EXPECT_EQ(model.GetState(), EditorPlayState::Stopped);
        EXPECT_EQ(model.GetStepCount(), 0u);
        EXPECT_FALSE(model.CanPause());
        EXPECT_FALSE(model.CanStep());
    }

    TEST(EditorPlayModelTest, PlayTogglesPlayAndStop) {
        EditorPlayModel model;
        model.PressPlay();
        EXPECT_EQ(model.GetState(), EditorPlayState::Playing);
        EXPECT_TRUE(model.CanPause());
        EXPECT_FALSE(model.CanStep());

        model.PressPlay();
        EXPECT_EQ(model.GetState(), EditorPlayState::Stopped);
    }

    TEST(EditorPlayModelTest, PlayStopsAPausedGame) {
        EditorPlayModel model;
        model.PressPlay();
        model.PressPause();
        ASSERT_EQ(model.GetState(), EditorPlayState::Paused);
        model.PressPlay();
        EXPECT_EQ(model.GetState(), EditorPlayState::Stopped);
    }

    TEST(EditorPlayModelTest, PauseTogglesPlayingAndPaused) {
        EditorPlayModel model;
        model.PressPlay();
        model.PressPause();
        EXPECT_EQ(model.GetState(), EditorPlayState::Paused);
        EXPECT_TRUE(model.CanPause());
        EXPECT_TRUE(model.CanStep());

        model.PressPause();
        EXPECT_EQ(model.GetState(), EditorPlayState::Playing);
    }

    TEST(EditorPlayModelTest, PauseIsIgnoredWhileStopped) {
        EditorPlayModel model;
        model.PressPause();
        EXPECT_EQ(model.GetState(), EditorPlayState::Stopped);
    }

    TEST(EditorPlayModelTest, StepCountsOnlyWhilePaused) {
        EditorPlayModel model;
        model.PressStep();
        EXPECT_EQ(model.GetStepCount(), 0u);

        model.PressPlay();
        model.PressStep();
        EXPECT_EQ(model.GetStepCount(), 0u);
        EXPECT_EQ(model.GetState(), EditorPlayState::Playing);

        model.PressPause();
        model.PressStep();
        model.PressStep();
        EXPECT_EQ(model.GetStepCount(), 2u);
        EXPECT_EQ(model.GetState(), EditorPlayState::Paused);

        model.PressPause();
        model.PressStep();
        EXPECT_EQ(model.GetStepCount(), 2u);
        EXPECT_EQ(model.GetState(), EditorPlayState::Playing);
    }

    TEST(EditorPlayModelTest, StoppingResetsTheStepCount) {
        EditorPlayModel model;
        model.PressPlay();
        model.PressPause();
        model.PressStep();
        ASSERT_EQ(model.GetStepCount(), 1u);

        model.PressPlay();
        EXPECT_EQ(model.GetState(), EditorPlayState::Stopped);
        EXPECT_EQ(model.GetStepCount(), 0u);
    }

}  // namespace BECore::Tests
