#include <BECore/Animation/TweenManager.h>
#include <Common/AllocCheck.h>
#include <Common/PoolCounters.h>
#include <Stress/Components/StressEntitiesComponent.h>
#include <Stress/StressNames.h>
#include <gtest/gtest.h>
#include <tests/StressSceneFixture.h>

namespace BECore::Stress::Tests {

    namespace {

        constexpr uint32_t kCount = 200;
        constexpr float kStep = 1.0f / 32.0f;
        /// A step that is longer than any orbit: every tween passes the end of a turn in it
        constexpr float kLongerThanAnOrbit = kOrbitMaxSeconds + 1.0f;
        /// Frames of kStep in the longest turn
        constexpr int kFramesOfALongestTurn = static_cast<int>(kOrbitMaxSeconds / kStep);

        TweenManager& Tweens() {
            return CoreManager::GetTweens();
        }

    }  // namespace

    class StressEntitiesTest : public StressSceneFixture {
    protected:
        void StartEntities(uint32_t count = kCount) {
            StartGame({"--stress-scene=entities", Format("--stress-count={}", count).c_str()});
            ASSERT_EQ(_manager->GetActiveScene()->GetName(), StressNames::entities);
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

        void ExpectOneStepToMoveEveryNode(size_t count) const {
            Vector<Pair<float, float>> before;
            for (size_t i = 0; i < count; ++i) {
                before.emplace_back(Transform(i)._x, Transform(i)._y);
            }

            Tweens().Advance(kStep);

            for (size_t i = 0; i < count; ++i) {
                EXPECT_TRUE(Transform(i)._x != before[i].first || Transform(i)._y != before[i].second) << i;
            }
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
        Vector<const SceneNode*> first;
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

        ExpectOneStepToMoveEveryNode(50);
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

    TEST_F(StressEntitiesTest, TheNodesKeepMovingAfterTheirTurnsEnd) {
        StartEntities(50);
        Update();

        Tweens().Advance(kLongerThanAnOrbit);
        for (int frame = 0; frame < 2 * kFramesOfALongestTurn; ++frame) {
            Tweens().Advance(kStep);
        }
        ASSERT_EQ(Tweens().GetActiveCount(), 50u);
        ExpectOneStepToMoveEveryNode(50);
    }

    TEST_F(StressEntitiesTest, ASteadyFrameStartsNoTweenAndAllocatesNothing) {
        StartEntities();
        const auto frames = [this](int count) {
            for (int frame = 0; frame < count; ++frame) {
                Update();
                Tweens().Advance(kStep);
            }
        };
        frames(3 * kFramesOfALongestTurn);  // the field and every list of the frame have their size
        const uint64_t handles = BECore::Tests::PoolAllocations("TweenHandle");
        ASSERT_NE(handles, 0u);

        const BECore::Tests::AllocCount allocated = BECore::Tests::CountAllocations([&] { frames(kFramesOfALongestTurn); });

        EXPECT_EQ(BECore::Tests::PoolAllocations("TweenHandle"), handles) << "no tween was started again";
        EXPECT_EQ(allocated.count, 0u) << allocated.bytes << " bytes";  // always zero without the tracker (macos-profile has it)
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
        _manager->SetActiveScene(StressNames::sprites);
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
