#include "SceneDrawListTest.h"

#include <BECore/Audio/AudioSourceComponent.h>
#include <BECore/Scene/Components/ChangeToRandomColorDebugComponent.h>
#include <BECore/Scene/Components/ClipRectComponent.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <Common/EngineEnvironment.h>
#include <Generated/SceneDrawListTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    class SceneDrawListTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _scene = BECore::New<Scene>();
            _scene->Initialize();
            _node = _scene->CreateNode("Node"_intern, nullptr);
        }

        IntrusivePtr<Scene> _scene;
        IntrusivePtr<SceneNode> _node;
    };

    TEST_F(SceneDrawListTest, TheBuiltInsThatNeverDrawStayOutOfTheList) {
        EXPECT_FALSE(TransformComponent().HasOnDraw());
        EXPECT_FALSE(QuadRendererComponent().HasOnDraw());
        EXPECT_FALSE(SpriteRendererComponent().HasOnDraw());
        EXPECT_FALSE(ClipRectComponent().HasOnDraw());
        EXPECT_FALSE(AudioSourceComponent().HasOnDraw());
        EXPECT_FALSE(ChangeToRandomColorDebugComponent().HasOnDraw());
    }

    TEST_F(SceneDrawListTest, AComponentThatDrawsOrSaysNothingStaysIn) {
        EXPECT_TRUE(OnDrawTestComponent().HasOnDraw());
        EXPECT_TRUE(PlainTestComponent().HasOnDraw()) << "a component that overrides nothing is in the list: false is an opt-out";
    }

    TEST_F(SceneDrawListTest, TheListIsStaleUntilTheFirstBuild) {
        EXPECT_FALSE(_scene->IsDrawListValid());
    }

    TEST_F(SceneDrawListTest, AComponentAddedOrRemovedMakesTheListStale) {
        _node->AddComponent(BECore::New<TransformComponent>());
        EXPECT_FALSE(_scene->IsDrawListValid());
        _node->RemoveComponent("TransformComponent"_intern);
        EXPECT_FALSE(_scene->IsDrawListValid());
    }

    TEST_F(SceneDrawListTest, ChangedEditorFlagsMakeTheListStale) {
        _node->SetEditorFlags(EditorFlags::Hidden);
        EXPECT_FALSE(_scene->IsDrawListValid());
    }

}  // namespace BECore::Tests
