#include <Common/Printers.h>
#include <Widgets/EditorLayout/EditorPlayModel.h>

namespace BECore::Tests {

    TEST(EditorPlayModelTest, StartsStoppedAndCanPause) {
        const EditorPlayModel model;
        EXPECT_EQ(model.GetState(), EditorPlayState::Stopped);
        EXPECT_EQ(model.GetStepCount(), 0u);
        EXPECT_TRUE(model.CanPause());
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

        // The pause that was left behind does not come back: the next pause starts from Stopped
        model.PressPause();
        model.PressPause();
        EXPECT_EQ(model.GetState(), EditorPlayState::Stopped);
    }

    TEST(EditorPlayModelTest, PauseWorksFromStoppedAndReturnsThere) {
        EditorPlayModel model;
        model.PressPause();
        EXPECT_EQ(model.GetState(), EditorPlayState::Paused);
        EXPECT_TRUE(model.CanStep());

        model.PressPause();
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

    TEST(EditorPlayModelTest, StepCountsOnlyWhilePausedAndSaysIfItIsDue) {
        EditorPlayModel model;
        EXPECT_FALSE(model.PressStep());
        EXPECT_EQ(model.GetStepCount(), 0u);

        model.PressPlay();
        EXPECT_FALSE(model.PressStep());
        EXPECT_EQ(model.GetStepCount(), 0u);
        EXPECT_EQ(model.GetState(), EditorPlayState::Playing);

        model.PressPause();
        EXPECT_TRUE(model.PressStep());
        EXPECT_TRUE(model.PressStep());
        EXPECT_EQ(model.GetStepCount(), 2u);
        EXPECT_EQ(model.GetState(), EditorPlayState::Paused);

        model.PressPause();
        EXPECT_FALSE(model.PressStep());
        EXPECT_EQ(model.GetStepCount(), 2u);
        EXPECT_EQ(model.GetState(), EditorPlayState::Playing);
    }

    TEST(EditorPlayModelTest, StoppingResetsTheStepCount) {
        EditorPlayModel model;
        model.PressPlay();
        model.PressPause();
        EXPECT_TRUE(model.PressStep());
        ASSERT_EQ(model.GetStepCount(), 1u);

        model.PressPlay();
        EXPECT_EQ(model.GetState(), EditorPlayState::Stopped);
        EXPECT_EQ(model.GetStepCount(), 0u);
    }

    TEST(EditorPlayModelTest, SetPausedCopiesThePauseOfTheEngineBothWays) {
        EditorPlayModel model;
        model.SetPaused(true);
        EXPECT_EQ(model.GetState(), EditorPlayState::Paused);
        model.SetPaused(true);
        EXPECT_EQ(model.GetState(), EditorPlayState::Paused);
        model.SetPaused(false);
        EXPECT_EQ(model.GetState(), EditorPlayState::Stopped);
        model.SetPaused(false);
        EXPECT_EQ(model.GetState(), EditorPlayState::Stopped);

        model.PressPlay();
        model.SetPaused(true);
        ASSERT_EQ(model.GetState(), EditorPlayState::Paused);
        model.SetPaused(false);
        EXPECT_EQ(model.GetState(), EditorPlayState::Playing);
    }

    TEST(EditorPlayModelTest, SetPausedDoesNotCountSteps) {
        EditorPlayModel model;
        model.SetPaused(true);
        model.SetPaused(false);
        EXPECT_EQ(model.GetStepCount(), 0u);
    }

}  // namespace BECore::Tests
