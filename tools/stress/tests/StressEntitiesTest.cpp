#include "StressSceneFixture.h"

#include <BECore/Animation/TweenManager.h>
#include <Stress/Components/StressEntitiesComponent.h>
#include <gtest/gtest.h>

/**
 * Coverage of StressEntitiesComponent on the shipped scene file: a node and a running tween per count, a field that grows and shrinks from the
 * tail, a tween that starts again when it ends, nothing drawn, and no tween left when the scene goes.
 */
namespace BECore::Stress::Tests {

    namespace {

        constexpr uint32_t kCount = 200;
        constexpr float kStep = 1.0f / 32.0f;
        /// A step that is longer than any orbit: every tween ends in it
        constexpr float kLongerThanAnOrbit = kOrbitMaxSeconds + 1.0f;

        TweenManager& Tweens() {
            return CoreManager::GetTweens();
        }

    }  // namespace

    class StressEntitiesTest : public StressSceneFixture {
    protected:
        void StartEntities(uint32_t count = kCount) {
            StartGame({"--stress-scene=entities", Format("--stress-count={}", count).c_str()});
            ASSERT_EQ(_manager->GetActiveScene()->GetName(), "StressEntities"_intern);
        }

        void TearDown() override {
            StressSceneFixture::TearDown();
            Tweens().CancelAll();  // a failed test must not leave tweens for the next one
        }

        SceneNode& Field() const {
            return StressSceneFixture::Field("StressEntities");
        }

        StressEntitiesComponent& Component() const {
            auto component = Field().GetComponent<StressEntitiesComponent>();
            EXPECT_TRUE(component);
            return *component;
        }

        TransformComponent& Transform(size_t index) const {
            return *Field().GetChildren()[index]->GetComponent<TransformComponent>();
        }
    };

    TEST_F(StressEntitiesTest, EveryNodeHasATransformAndARunningTween) {
        StartEntities();
        Update();

        EXPECT_EQ(Field().GetChildren().size(), size_t{kCount});
        EXPECT_EQ(Component().GetEntityCount(), size_t{kCount});
        EXPECT_EQ(Tweens().GetActiveCount(), size_t{kCount});
        for (const auto& node : Field().GetChildren()) {
            EXPECT_TRUE(node->GetComponent<TransformComponent>());
            EXPECT_EQ(node->GetComponents().size(), 1u) << "a transform and nothing else: nothing to draw";
        }
    }

    TEST_F(StressEntitiesTest, NothingIsDrawn) {
        StartEntities();
        Update();
        Draw();

        EXPECT_TRUE(_renderer->calls.empty());
    }

    TEST_F(StressEntitiesTest, GrowAddsOnlyAndShrinkCancelsTheTailsTweens) {
        StartEntities(100);
        Update();
        eastl::vector<const SceneNode*> first;
        for (size_t i = 0; i < 10; ++i) {
            first.push_back(Field().GetChildren()[i].Get());
        }

        SetCount(300);
        Update();
        EXPECT_EQ(Field().GetChildren().size(), 300u);
        EXPECT_EQ(Tweens().GetActiveCount(), 300u);
        for (size_t i = 0; i < first.size(); ++i) {
            EXPECT_EQ(Field().GetChildren()[i].Get(), first[i]) << i;
        }

        SetCount(40);
        Update();
        EXPECT_EQ(Field().GetChildren().size(), 40u);
        EXPECT_EQ(Tweens().GetActiveCount(), 40u) << "the count of the tweens drops with the nodes";
        for (size_t i = 0; i < first.size(); ++i) {
            EXPECT_EQ(Field().GetChildren()[i].Get(), first[i]) << i;
        }

        SetCount(0);
        Update();
        EXPECT_TRUE(Field().GetChildren().empty());
        EXPECT_EQ(Tweens().GetActiveCount(), 0u);
    }

    TEST_F(StressEntitiesTest, TheNodesMoveAfterTheTweensAdvance) {
        StartEntities(50);
        Update();
        Tweens().Advance(kStep);  // the first step writes the positions
        eastl::vector<std::pair<float, float>> before;
        for (size_t i = 0; i < 50; ++i) {
            before.emplace_back(Transform(i)._x, Transform(i)._y);
        }

        Tweens().Advance(kStep);

        for (size_t i = 0; i < 50; ++i) {
            EXPECT_TRUE(Transform(i)._x != before[i].first || Transform(i)._y != before[i].second) << i;
        }
    }

    TEST_F(StressEntitiesTest, ANodeStaysOnItsOrbit) {
        StartEntities(20);
        Update();
        for (int step = 0; step < 100; ++step) {
            Tweens().Advance(kStep);
        }
        const FieldView view{GetScene("StressEntities").GetViewWidth(), GetScene("StressEntities").GetViewHeight()};
        for (uint32_t i = 0; i < 20; ++i) {
            const EntityOrbit orbit = OrbitOf(i, view);
            const float dx = Transform(i)._x - orbit.centerX;
            const float dy = Transform(i)._y - orbit.centerY;
            EXPECT_NEAR(std::sqrt(dx * dx + dy * dy), orbit.radius, 0.01f) << i;
        }
    }

    TEST_F(StressEntitiesTest, ATweenThatEndsStartsAgain) {
        StartEntities(50);
        Update();

        Tweens().Advance(kLongerThanAnOrbit);  // every tween completes; its onComplete starts the next one
        EXPECT_EQ(Tweens().GetActiveCount(), 50u);

        for (int frame = 0; frame < 400; ++frame) {  // many frames: every tween ends at least once more, one frame per link
            Tweens().Advance(kStep);
            ASSERT_EQ(Tweens().GetActiveCount(), 50u) << "frame " << frame;
        }
        EXPECT_EQ(Field().GetChildren().size(), 50u);
    }

    TEST_F(StressEntitiesTest, TheRestartsOfAFreshFieldAreSpreadFromItsFirstFrames) {
        constexpr uint32_t kNodes = 2000;
        constexpr uint32_t kFrames = 120;  // 2 s: the shortest orbit, so no node has yet ended a full first turn
        StartEntities(kNodes);
        Update();
        ASSERT_EQ(Component().GetRestartCount(), 0u);

        uint64_t restarts = Component().GetRestartCount();
        uint64_t mostInAFrame = 0;
        uint64_t framesWithRestarts = 0;
        for (uint32_t frame = 0; frame < kFrames; ++frame) {
            Tweens().Advance(1.0f / 60.0f);
            const uint64_t now = Component().GetRestartCount();
            mostInAFrame = std::max(mostInAFrame, now - restarts);
            framesWithRestarts += now > restarts ? 1u : 0u;
            restarts = now;
        }

        const double perFrame = static_cast<double>(restarts) / kFrames;
        EXPECT_GT(perFrame, 5.0) << "a full-duration first tween would restart nothing in these two seconds";
        EXPECT_GE(framesWithRestarts, uint64_t{kFrames} * 9 / 10) << "nearly every frame restarts some: no stretch of quiet frames";
        EXPECT_LT(static_cast<double>(mostInAFrame), 3.0 * perFrame) << "no frame is a burst";
    }

    TEST_F(StressEntitiesTest, EveryKthNodeTurnsOnUpdate) {
        StartEntities(40);
        Update();
        Update();

        for (size_t i = 0; i < 40; ++i) {
            if (i % kSpinEvery == 0) {
                EXPECT_GT(Transform(i)._rotation, 0.0f) << i;
            } else {
                EXPECT_EQ(Transform(i)._rotation, 0.0f) << i;
            }
        }
    }

    TEST_F(StressEntitiesTest, AGameThatIsNotActiveBuildsNothing) {
        StartEntities();
        GameManager::GetInstance().Shutdown();
        Update();

        EXPECT_TRUE(Field().GetChildren().empty());
        EXPECT_EQ(Tweens().GetActiveCount(), 0u);
    }

    TEST_F(StressEntitiesTest, AnotherActiveSceneDoesNotBuildTheField) {
        StartEntities();
        _manager->SetActiveScene("StressSprites"_intern);
        Update();

        EXPECT_TRUE(Field().GetChildren().empty());
        EXPECT_EQ(Tweens().GetActiveCount(), 0u);
    }

    TEST_F(StressEntitiesTest, DestroyingTheSceneLeavesNoTween) {
        StartEntities();
        Update();
        Tweens().Advance(kStep);
        ASSERT_EQ(Tweens().GetActiveCount(), size_t{kCount});

        BECore::Tests::EngineEnvironment::UseSceneManager({});
        GameManager::GetInstance().Shutdown();
        _manager.Reset();

        EXPECT_EQ(Tweens().GetActiveCount(), 0u);
    }

}  // namespace BECore::Stress::Tests
