#include <BECore/Animation/TweenManager.h>
#include <BECore/RefCounted/New.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <Common/EngineEnvironment.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int kChildCount = 3;
        constexpr float kTweenTo = 1.0f;
        constexpr float kTweenSeconds = 1.0f;

        /// Compares the pool of T by its own counter: other tests of the process make the same types, so no process-wide total is read
        template <typename T>
        struct PoolWatch {
            size_t before = T::PooledPool().GetLiveCount();

            [[nodiscard]] size_t Added() const {
                return T::PooledPool().GetLiveCount() - before;
            }
        };

    }  // namespace

    /// Scenes need the core services, like the other scene tests
    class PooledOptInTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }
    };

    TEST_F(PooledOptInTest, ATreeOfNodesWithComponentsGivesEveryBlockBackWhenItIsReleased) {
        const PoolWatch<SceneNode> nodes;
        const PoolWatch<TransformComponent> transforms;
        const PoolWatch<SpriteRendererComponent> sprites;
        {
            auto scene = New<Scene>();
            scene->Initialize();
            const size_t baseNodes = nodes.Added();
            for (int i = 0; i < kChildCount; ++i) {
                auto child = scene->CreateNode("Child"_intern, nullptr);
                child->AddComponent(New<TransformComponent>());
                child->AddComponent(New<SpriteRendererComponent>());
            }
            EXPECT_EQ(nodes.Added(), baseNodes + static_cast<size_t>(kChildCount));
            EXPECT_EQ(transforms.Added(), static_cast<size_t>(kChildCount));
            EXPECT_EQ(sprites.Added(), static_cast<size_t>(kChildCount));
        }
        EXPECT_EQ(nodes.Added(), 0u);
        EXPECT_EQ(transforms.Added(), 0u);
        EXPECT_EQ(sprites.Added(), 0u);
    }

    TEST_F(PooledOptInTest, ASceneNodeBlockIsHandedOutAgainAfterItsRelease) {
        const SceneNode* address = nullptr;
        {
            auto node = New<SceneNode>();
            address = node.Get();
        }
        EXPECT_EQ(New<SceneNode>().Get(), address);
    }

    TEST_F(PooledOptInTest, ATweenHandleGivesItsBlockBackWhenTheTweenEndsAndTheHandleIsDropped) {
        const PoolWatch<TweenHandle> handles;
        TweenManager tweens;
        const TweenHandle* address = nullptr;
        float value = 0.0f;
        {
            auto handle = tweens.Start<float>(0.0f, kTweenTo, TweenSettings{kTweenSeconds, 0.0f, EaseType::Linear}, [&value](const float& v) { value = v; });
            address = handle.Get();
            EXPECT_EQ(handles.Added(), 1u);
            tweens.Advance(kTweenSeconds);
            EXPECT_TRUE(handle->IsFinished());
            EXPECT_EQ(handles.Added(), 1u) << "the caller's handle keeps the block";
        }
        EXPECT_EQ(value, kTweenTo);
        EXPECT_EQ(handles.Added(), 0u);

        auto next = tweens.Start<float>(0.0f, kTweenTo, TweenSettings{kTweenSeconds, 0.0f, EaseType::Linear}, [](const float&) {});
        EXPECT_EQ(next.Get(), address);
    }

}  // namespace BECore::Tests
