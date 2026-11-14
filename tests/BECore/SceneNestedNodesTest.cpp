#include "SceneEventsTest.h"

#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Events/SceneEvents.h>
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

        /// The names of the tree below @p node, depth first, each with the number of its ancestors below @p node ("Left Inner. Deep.. Last.")
        void AppendTree(const SceneNode& node, size_t depth, String& out) {
            for (const auto& child : node.GetChildren()) {
                if (!out.empty()) {
                    out.append(" ");
                }
                out.append(child->GetName().CStr());
                out.append(depth, '.');
                AppendTree(*child, depth + 1, out);
            }
        }

        String TreeOf(const SceneNode& root) {
            String out;
            AppendTree(root, 0, out);
            return out;
        }

        SceneNode* FindByName(SceneNode& from, PoolString name) {
            for (const auto& child : from.GetChildren()) {
                if (child->GetName() == name) {
                    return child.Get();
                }
                if (SceneNode* found = FindByName(*child, name)) {
                    return found;
                }
            }
            return nullptr;
        }

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
        SceneNode* inner = FindByName(scene.GetRootNode(), "Inner"_intern);
        ASSERT_NE(inner, nullptr);
        EXPECT_EQ(inner->GetLayer(), kInnerLayer);
        EXPECT_EQ(inner->GetParent()->GetName(), "Left"_intern);
        EXPECT_EQ(FindByName(scene.GetRootNode(), "Deep"_intern)->GetParent(), inner);
        EXPECT_EQ(FindByName(scene.GetRootNode(), "Left"_intern)->GetParent(), &scene.GetRootNode());
    }

    TEST_F(SceneNestedNodesTest, EveryNestedNodeIsWiredLikeATopLevelOne) {
        Scene& scene = Load();

        SceneNode* inner = FindByName(scene.GetRootNode(), "Inner"_intern);
        SceneNode* deep = FindByName(scene.GetRootNode(), "Deep"_intern);
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
        const auto top = FindByName(scene.GetRootNode(), "Left"_intern)->GetComponent<SceneEventCounter>();
        const auto nested = FindByName(scene.GetRootNode(), "Inner"_intern)->GetComponent<SceneEventCounter>();
        ASSERT_TRUE(top);
        ASSERT_TRUE(nested);

        // The counter subscribes in OnAttached: a second OnAttached would count an update twice
        SceneEvents::SceneUpdateEvent::Emit();

        EXPECT_EQ(top->updateCount, 1);
        EXPECT_EQ(nested->updateCount, 1);
    }

    TEST_F(SceneNestedNodesTest, TheDrawOrderFollowsTheTree) {
        Scene& scene = Load();
        const SceneNode& left = *FindByName(scene.GetRootNode(), "Left"_intern);
        const SceneNode& last = *FindByName(scene.GetRootNode(), "Last"_intern);
        const SceneNode& right = *FindByName(scene.GetRootNode(), "Right"_intern);
        const SceneNode& inner = *FindByName(scene.GetRootNode(), "Inner"_intern);

        // Layer 0 in tree order, the node on layer 3 after all of them
        EXPECT_LT(scene.GetDrawRank(left), scene.GetDrawRank(last));
        EXPECT_LT(scene.GetDrawRank(last), scene.GetDrawRank(right));
        EXPECT_GT(scene.GetDrawRank(inner), scene.GetDrawRank(right));
    }

}  // namespace BECore::Tests
