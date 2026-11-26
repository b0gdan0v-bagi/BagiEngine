#include <BECore/Animation/TweenManager.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <Demo/Components/DemoTweenComponent.h>
#include <tests/DemoSceneFixture.h>

namespace BECore::Demo::Tests {

    namespace {

        constexpr const char* kMover = "SlidingStar";
        constexpr int kSomeFrames = 24;
        constexpr float kTolerance = 1e-3f;

    }  // namespace

    class DemoTweenTest : public DemoSceneFixture {
    protected:
        void Run(int frames) {
            for (int i = 0; i < frames; ++i) {
                Update();
            }
        }

        void SetAnimate(StringView value) {
            CoreManager::GetUIVars().Set(DemoNames::animateVar, value);
        }
    };

    TEST_F(DemoTweenTest, TheTweenMovesAndTurnsItsNodeAndKeepsAHandEdit) {
        LoadScene();
        auto& transform = Component<TransformComponent>(kMover);
        const float authoredX = transform._x;

        Run(kSomeFrames);
        const float added = transform._x - authoredX;
        ASSERT_GT(added, 1.0f);
        EXPECT_GT(transform._rotation, 1.0f);
        const auto run = Component<DemoTweenComponent>(kMover).GetTween();
        ASSERT_TRUE(run);

        constexpr float kTyped = 300.0f;
        transform._x = kTyped;  // a drag or a typed x in the editor
        Update();
        // One frame of the way further, from the typed value and not from the authored one
        const float nextAdded = added + (transform._x - kTyped);
        EXPECT_NE(transform._x, kTyped);
        EXPECT_GT(nextAdded, added);
        EXPECT_LT(nextAdded - added, Component<DemoTweenComponent>(kMover)._offsetX * 0.1f);
        EXPECT_EQ(Component<DemoTweenComponent>(kMover).GetTween(), run) << "an unchanged frame starts nothing";

        SetAnimate(DemoNames::off);
        Update();
        EXPECT_NEAR(transform._x, kTyped - added, kTolerance) << "the edit stays, without what the tween had added then";
    }

    TEST_F(DemoTweenTest, SwitchedOffItGivesTheAuthoredPlaceBack) {
        LoadScene();
        auto& transform = Component<TransformComponent>(kMover);
        const float x = transform._x;
        const float y = transform._y;
        const float rotation = transform._rotation;
        Run(kSomeFrames);
        ASSERT_NE(transform._x, x);

        SetAnimate(DemoNames::off);
        Update();
        EXPECT_EQ(transform._x, x);
        EXPECT_EQ(transform._y, y);
        EXPECT_EQ(transform._rotation, rotation);
        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 0u);
        Run(kSomeFrames);
        EXPECT_EQ(transform._x, x);

        SetAnimate(DemoNames::on);
        Run(kSomeFrames);
        EXPECT_GT(transform._x, x);
    }

    TEST_F(DemoTweenTest, AnEditedDurationTakesEffect) {
        LoadScene();
        auto& transform = Component<TransformComponent>(kMover);
        auto& tween = Component<DemoTweenComponent>(kMover);
        const float authoredX = transform._x;
        Run(kSomeFrames);

        tween._duration = 0.5f;
        tween._ease = EaseType::Linear;
        Update();  // the new run starts at the authored place
        EXPECT_NEAR(transform._x, authoredX, kTolerance);
        Run(8);  // a quarter of a second: half of the new way
        EXPECT_NEAR(transform._x, authoredX + tween._offsetX * 0.5f, kTolerance);
        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 3u) << "the old run is cancelled";
    }

    TEST_F(DemoTweenTest, ADestroyedSceneLeavesNoRunningTween) {
        LoadScene();
        Update();
        ASSERT_GT(CoreManager::GetTweens().GetActiveCount(), 0u);

        BECore::Tests::EngineEnvironment::UseSceneManager({});
        _manager.Reset();
        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 0u);
    }

}  // namespace BECore::Demo::Tests
