#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/SceneEventsTest.h>
#include <Common/EngineEnvironment.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Events/SceneEvents.h>
#include <entt/entt.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Root holds Left and Right; Left holds Inner (layer 3, with a counter and a transform) and Last; Inner holds Deep
        constexpr const char* kNestedScene = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Left"><components>
      <component type="SceneEventCounter"/>
    </components><children>
      <node name="Inner" layer="3"><components>
        <component type="TransformComponent" x="5" y="6" width="7" height="8"/>
        <component type="SceneEventCounter"/>
      </components><children>
        <node name="Deep"/>
      </children></node>
      <node name="Last"/>
    </children></node>
    <node name="Right"/>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr int32_t kInnerLayer = 3;

    }  // namespace

    /// Nested `<children>` in the scene file: loading and the wiring of the nested nodes (the save is in SceneEditorSaveTest)
    class SceneNestedNodesTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void TearDown() override {
            _manager.Reset();
        }

        Scene& Load() {
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(kNestedScene));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            EXPECT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            return *_manager->GetSceneByName("Main"_intern);
        }

        IntrusivePtrAtomic<SceneManager> _manager;
    };

    TEST_F(SceneNestedNodesTest, LoadingBuildsTheTreeInFileOrder) {
        Scene& scene = Load();

        EXPECT_EQ(TreeOf(scene.GetRootNode()), "Left Inner. Deep.. Last. Right");
        SceneNode* inner = FindNodeBelow(scene.GetRootNode(), "Inner");
        ASSERT_NE(inner, nullptr);
        EXPECT_EQ(inner->GetLayer(), kInnerLayer);
        EXPECT_EQ(inner->GetParent()->GetName(), "Left"_intern);
        EXPECT_EQ(Require(FindNodeBelow(scene.GetRootNode(), "Deep")).GetParent(), inner);
        EXPECT_EQ(Require(FindNodeBelow(scene.GetRootNode(), "Left")).GetParent(), &scene.GetRootNode());
    }

    TEST_F(SceneNestedNodesTest, EveryNestedNodeIsWiredLikeATopLevelOne) {
        Scene& scene = Load();

        SceneNode* inner = FindNodeBelow(scene.GetRootNode(), "Inner");
        SceneNode* deep = FindNodeBelow(scene.GetRootNode(), "Deep");
        ASSERT_NE(inner, nullptr);
        ASSERT_NE(deep, nullptr);
        EXPECT_TRUE(scene.GetRegistry().valid(inner->GetEntity()));
        EXPECT_TRUE(scene.GetRegistry().valid(deep->GetEntity()));
        EXPECT_NE(inner->GetEntity(), deep->GetEntity());
        for (const auto& component : inner->GetComponents()) {
            EXPECT_EQ(&component->GetNode(), inner);
            EXPECT_EQ(&component->GetScene(), &scene);
        }
    }

    TEST_F(SceneNestedNodesTest, OnAttachedRunsOnceForANestedComponent) {
        Scene& scene = Load();
        const auto top = Require(FindNodeBelow(scene.GetRootNode(), "Left")).GetComponent<SceneEventCounter>();
        const auto nested = Require(FindNodeBelow(scene.GetRootNode(), "Inner")).GetComponent<SceneEventCounter>();
        ASSERT_TRUE(top);
        ASSERT_TRUE(nested);

        // The counter subscribes in OnAttached: a second OnAttached would count an update twice
        SceneEvents::SceneUpdateEvent::Emit();

        EXPECT_EQ(top->updateCount, 1);
        EXPECT_EQ(nested->updateCount, 1);
    }

    TEST_F(SceneNestedNodesTest, TheDrawOrderFollowsTheTree) {
        Scene& scene = Load();
        const SceneNode& left = Require(FindNodeBelow(scene.GetRootNode(), "Left"));
        const SceneNode& last = Require(FindNodeBelow(scene.GetRootNode(), "Last"));
        const SceneNode& right = Require(FindNodeBelow(scene.GetRootNode(), "Right"));
        const SceneNode& inner = Require(FindNodeBelow(scene.GetRootNode(), "Inner"));

        // Layer 0 in tree order, the node on layer 3 after all of them
        EXPECT_LT(scene.GetDrawRank(left), scene.GetDrawRank(last));
        EXPECT_LT(scene.GetDrawRank(last), scene.GetDrawRank(right));
        EXPECT_GT(scene.GetDrawRank(inner), scene.GetDrawRank(right));
    }

}  // namespace BECore::Tests
