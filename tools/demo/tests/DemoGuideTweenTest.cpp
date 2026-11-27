#include <BECore/Animation/Easing.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <Common/Require.h>
#include <Demo/Components/DemoTweenComponent.h>
#include <cmath>
#include <tests/DemoSceneFixture.h>

namespace BECore::Demo::Tests {

    namespace {

        constexpr const char* kSlider = "Slider";
        constexpr const char* kTurner = "Turner";
        constexpr const char* kBobber = "Bobber";
        // The first Update starts a run (the tweens advance before the scene update, CoreManager::OnGameCycle), so a run has advanced one frame fewer than the Updates
        constexpr int kStartingFrame = 1;
        constexpr int kFramesOfTwoSeconds = 64;  // kFrame is 1/32
        constexpr int kFramesOfTheStart = 4;
        constexpr float kTolerance = 1e-3f;

        /// The guide page reads the example from the source tree, as the picture of the page does
        Path ExampleScene() {
            return Path(BagiContentRoots::kEngine) / "docs" / "guide" / "examples" / "tween" / "config" / "SceneConfig.xml";
        }

        /// 0 at the start of the way and 1 at the far end: where a yoyo of @p tween stands @p seconds after the start of its run (the delay first, then the
        /// way there along the curve, and every second leg back along the same curve)
        float Progress(const DemoTweenComponent& tween, float seconds) {
            const float run = seconds - tween._delay;
            if (run <= 0.0f) {
                return 0.0f;
            }
            const float legs = run / tween._duration;
            const float leg = std::floor(legs);
            const float along = legs - leg;
            const bool back = static_cast<int>(leg) % 2 == 1;
            return Ease(tween._ease, back ? 1.0f - along : along);
        }

    }  // namespace

    class DemoGuideTweenTest : public DemoSceneFixture {
    protected:
        void SetUp() override {
            ASSERT_NO_FATAL_FAILURE(LoadScene(ExampleScene()));
        }

        void Run(int frames) {
            for (int i = 0; i < frames; ++i) {
                Update();
            }
        }

        TransformComponent& TransformOf(const char* name) const {
            return BECore::Tests::Require(Node(name).GetComponent<TransformComponent>());
        }

        DemoTweenComponent& TweenOf(const char* name) const {
            return BECore::Tests::Require(Node(name).GetComponent<DemoTweenComponent>());
        }
    };

    TEST_F(DemoGuideTweenTest, EveryMoverHoldsATweenAfterOneFrame) {
        for (const char* name : {kSlider, kTurner, kBobber}) {
            EXPECT_FALSE(TweenOf(name).GetTween()) << name << " starts its run in the first update";
        }
        Update();
        for (const char* name : {kSlider, kTurner, kBobber}) {
            EXPECT_TRUE(TweenOf(name).GetTween()) << name;
        }
        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 3u);
    }

    TEST_F(DemoGuideTweenTest, AfterTwoSecondsTheNodesStandWhereTheFieldsSay) {
        const float sliderX = TransformOf(kSlider)._x;
        const float turnerRotation = TransformOf(kTurner)._rotation;
        const float bobberY = TransformOf(kBobber)._y;

        Run(kStartingFrame + kFramesOfTwoSeconds);

        const float seconds = static_cast<float>(kFramesOfTwoSeconds * kFrame);
        const DemoTweenComponent& slide = TweenOf(kSlider);
        EXPECT_NEAR(TransformOf(kSlider)._x, sliderX + slide._offsetX * Progress(slide, seconds), kTolerance);
        EXPECT_NEAR(TransformOf(kSlider)._x, sliderX + slide._offsetX, kTolerance) << "two seconds is the far end of the slider";
        const DemoTweenComponent& turn = TweenOf(kTurner);
        EXPECT_NEAR(TransformOf(kTurner)._rotation, turnerRotation + turn._turn * Progress(turn, seconds), kTolerance);
        EXPECT_NEAR(TransformOf(kTurner)._rotation, turnerRotation + turn._turn, kTolerance) << "and of the turner";
        const DemoTweenComponent& bob = TweenOf(kBobber);
        EXPECT_NEAR(TransformOf(kBobber)._y, bobberY + bob._offsetY * Progress(bob, seconds), kTolerance);
        EXPECT_GT(TransformOf(kBobber)._y, bobberY + bob._offsetY) << "the bobber is on its way back, not at its far end";
    }

    TEST_F(DemoGuideTweenTest, TheDelayHoldsTheFirstMoveAndTheOthersAlreadyMove) {
        const float sliderX = TransformOf(kSlider)._x;
        const float bobberY = TransformOf(kBobber)._y;
        const DemoTweenComponent& bob = TweenOf(kBobber);
        const float seconds = static_cast<float>(kFramesOfTheStart * kFrame);
        ASSERT_LT(seconds, bob._delay);

        Run(kStartingFrame + kFramesOfTheStart);

        EXPECT_EQ(TransformOf(kBobber)._y, bobberY);
        const DemoTweenComponent& slide = TweenOf(kSlider);
        EXPECT_NEAR(TransformOf(kSlider)._x, sliderX + slide._offsetX * Progress(slide, seconds), kTolerance);
        EXPECT_GT(TransformOf(kSlider)._x, sliderX);
    }

}  // namespace BECore::Demo::Tests
