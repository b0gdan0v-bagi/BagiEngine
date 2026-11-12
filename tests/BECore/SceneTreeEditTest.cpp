#include "SceneEventsTest.h"

#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Events/SceneEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // A, B and C on the root; B holds B1 and B2 (B1 holds B1a); A and C carry a counter that subscribes in OnAttached
        constexpr const char* kTreeScene = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="A"><components>
      <component type="TransformComponent" x="1" y="2" width="3" height="4"/>
      <component type="SceneEventCounter"/>
    </components></node>
    <node name="B"><children>
      <node name="B1" layer="4"><children><node name="B1a"/></children></node>
      <node name="B2"/>
    </children></node>
    <node name="C"><components><component type="SceneEventCounter"/></components></node>
  </nodes></scene>
  <scene type="Scene" name="Other"><nodes><node name="Foreign"/></nodes></scene>
</scenes></SceneManager>)";

        constexpr float kEditedX = 99.0f;
        constexpr float kOriginalX = 1.0f;

        /// The names of the tree below @p node, depth first, each followed by one dot per level ("A B B1. B1a.. B2. C")
        void AppendTree(const SceneNode& node, size_t depth, eastl::string& out) {
            for (const auto& child : node.GetChildren()) {
                if (!out.empty()) {
                    out.append(" ");
                }
                out.append(child->GetName().CStr());
                out.append(depth, '.');
                AppendTree(*child, depth + 1, out);
            }
        }

        eastl::string TreeOf(const SceneNode& root) {
            eastl::string out;
            AppendTree(root, 0, out);
            return out;
        }

        SceneNode* Find(SceneNode& from, const char* name) {
            const PoolString wanted = PoolString::Intern(name);
            for (const auto& child : from.GetChildren()) {
                if (child->GetName() == wanted) {
                    return child.Get();
                }
                if (SceneNode* found = Find(*child, name)) {
                    return found;
                }
            }
            return nullptr;
        }

    }  // namespace

    /// Scene::MoveNode, DuplicateNode and MakeUniqueSiblingName on a scene loaded from a string
    class SceneTreeEditTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kTreeScene));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _scene = _manager->GetSceneByName("Main"_intern);
            ASSERT_NE(_scene, nullptr);
        }

        void TearDown() override {
            _manager.Reset();
        }

        SceneNode& Node(const char* name) {
            SceneNode* node = Find(_scene->GetRootNode(), name);
            EXPECT_NE(node, nullptr) << name;
            return *node;
        }

        eastl::string Tree() const {
            return TreeOf(_scene->GetRootNode());
        }

        IntrusivePtrAtomic<SceneManager> _manager;
        Scene* _scene = nullptr;
    };

    TEST_F(SceneTreeEditTest, AMoveToAnotherParentAppendsByDefault) {
        ASSERT_TRUE(_scene->MoveNode(Node("A"), &Node("B")));

        EXPECT_EQ(Tree(), "B B1. B1a.. B2. A. C");
        EXPECT_EQ(Node("A").GetParent(), &Node("B"));
    }

    TEST_F(SceneTreeEditTest, AMoveToANullParentGoesToTheRoot) {
        ASSERT_TRUE(_scene->MoveNode(Node("B2"), nullptr, 0));

        EXPECT_EQ(Tree(), "B2 A B B1. B1a.. C");
        EXPECT_EQ(Node("B2").GetParent(), &_scene->GetRootNode());
    }

    TEST_F(SceneTreeEditTest, TheIndexCountsAfterTheNodeLeftItsPlace) {
        // Down inside one parent: A is taken out first, so index 2 is behind B and C
        ASSERT_TRUE(_scene->MoveNode(Node("A"), nullptr, 2));
        EXPECT_EQ(Tree(), "B B1. B1a.. B2. C A");

        // Up
        ASSERT_TRUE(_scene->MoveNode(Node("A"), nullptr, 0));
        EXPECT_EQ(Tree(), "A B B1. B1a.. B2. C");

        // One step down: behind the one that was after it
        ASSERT_TRUE(_scene->MoveNode(Node("A"), nullptr, 1));
        EXPECT_EQ(Tree(), "B B1. B1a.. B2. A C");

        // Siblings inside a node that is not the root
        ASSERT_TRUE(_scene->MoveNode(Node("B1"), &Node("B"), 1));
        EXPECT_EQ(Tree(), "B B2. B1. B1a.. A C");
    }

    TEST_F(SceneTreeEditTest, TheIndexIsClamped) {
        ASSERT_TRUE(_scene->MoveNode(Node("A"), &Node("B"), 99));
        EXPECT_EQ(Tree(), "B B1. B1a.. B2. A. C");

        ASSERT_TRUE(_scene->MoveNode(Node("C"), &Node("B"), Scene::kAppend));
        EXPECT_EQ(Tree(), "B B1. B1a.. B2. A. C.");
    }

    TEST_F(SceneTreeEditTest, ASubtreeMovesWithItsNode) {
        ASSERT_TRUE(_scene->MoveNode(Node("B1"), nullptr, 0));

        EXPECT_EQ(Tree(), "B1 B1a. A B B2. C");
        EXPECT_EQ(Node("B1a").GetParent(), &Node("B1"));
        EXPECT_TRUE(_scene->GetRegistry().valid(Node("B1a").GetEntity()));
    }

    TEST_F(SceneTreeEditTest, AMoveKeepsTheEntityAndDoesNotAttachAgain) {
        SceneNode& a = Node("A");
        const entt::entity entity = a.GetEntity();
        const auto counter = a.GetComponent<SceneEventCounter>();
        ASSERT_TRUE(counter);

        ASSERT_TRUE(_scene->MoveNode(a, &Node("B")));
        SceneEvents::SceneUpdateEvent::Emit();

        EXPECT_EQ(a.GetEntity(), entity);
        EXPECT_EQ(counter->updateCount, 1);
        EXPECT_EQ(&counter->GetNode(), &a);
    }

    TEST_F(SceneTreeEditTest, IntoItselfOrADescendantIsRefusedAndChangesNothing) {
        const eastl::string before = Tree();

        EXPECT_FALSE(_scene->MoveNode(Node("B"), &Node("B")));
        EXPECT_FALSE(_scene->MoveNode(Node("B"), &Node("B1")));
        EXPECT_FALSE(_scene->MoveNode(Node("B"), &Node("B1a"), 0));

        EXPECT_EQ(Tree(), before);
        EXPECT_EQ(Node("B").GetParent(), &_scene->GetRootNode());
    }

    TEST_F(SceneTreeEditTest, TheRootAndNodesOfAnotherSceneAreRefused) {
        Scene& other = *_manager->GetSceneByName("Other"_intern);
        SceneNode& foreign = *Find(other.GetRootNode(), "Foreign");
        const eastl::string before = Tree();
        const eastl::string otherBefore = TreeOf(other.GetRootNode());

        EXPECT_FALSE(_scene->MoveNode(_scene->GetRootNode(), &Node("B")));
        EXPECT_FALSE(_scene->MoveNode(foreign, &Node("B")));
        EXPECT_FALSE(_scene->MoveNode(Node("A"), &foreign));
        EXPECT_FALSE(other.MoveNode(Node("A"), nullptr));

        EXPECT_EQ(Tree(), before);
        EXPECT_EQ(TreeOf(other.GetRootNode()), otherBefore);
    }

    TEST_F(SceneTreeEditTest, TheDrawRankFollowsAMove) {
        // Layer 0 in tree order: A, B, C
        EXPECT_LT(_scene->GetDrawRank(Node("A")), _scene->GetDrawRank(Node("C")));

        ASSERT_TRUE(_scene->MoveNode(Node("A"), nullptr, Scene::kAppend));

        EXPECT_GT(_scene->GetDrawRank(Node("A")), _scene->GetDrawRank(Node("C")));
        EXPECT_GT(_scene->GetDrawRank(Node("A")), _scene->GetDrawRank(Node("B")));
    }

    TEST_F(SceneTreeEditTest, ADuplicateComesRightAfterTheOriginalWithItsSubtree) {
        const IntrusivePtr<SceneNode> copy = _scene->DuplicateNode(Node("B"));

        ASSERT_TRUE(copy);
        EXPECT_EQ(Tree(), "A B B1. B1a.. B2. B (copy) B1. B1a.. B2. C");
        EXPECT_EQ(copy->GetName(), "B (copy)"_intern);
        EXPECT_EQ(copy->GetParent(), &_scene->GetRootNode());
        EXPECT_EQ(copy->GetChildren()[0]->GetLayer(), 4);
        EXPECT_NE(copy.Get(), &Node("B"));
        EXPECT_EQ(copy->GetChildren()[0]->GetParent(), copy.Get());
    }

    TEST_F(SceneTreeEditTest, ADuplicateInsideAParentGoesAfterTheOriginalThere) {
        const IntrusivePtr<SceneNode> copy = _scene->DuplicateNode(Node("B1"));

        ASSERT_TRUE(copy);
        EXPECT_EQ(Tree(), "A B B1. B1a.. B1 (copy). B1a.. B2. C");
    }

    TEST_F(SceneTreeEditTest, ADuplicateIsWiredAndIndependent) {
        SceneNode& a = Node("A");
        const IntrusivePtr<SceneNode> copy = _scene->DuplicateNode(a);

        ASSERT_TRUE(copy);
        EXPECT_TRUE(_scene->GetRegistry().valid(copy->GetEntity()));
        EXPECT_NE(copy->GetEntity(), a.GetEntity());
        ASSERT_EQ(copy->GetComponents().size(), a.GetComponents().size());
        for (const auto& component : copy->GetComponents()) {
            EXPECT_EQ(&component->GetNode(), copy.Get());
            EXPECT_EQ(&component->GetScene(), _scene);
        }

        // OnAttached ran once for the copy's counter: one update, one count; the original's own count is its own
        SceneEvents::SceneUpdateEvent::Emit();
        EXPECT_EQ(copy->GetComponent<SceneEventCounter>()->updateCount, 1);
        EXPECT_EQ(a.GetComponent<SceneEventCounter>()->updateCount, 1);

        // Editing the copy leaves the original
        auto copyTransform = copy->GetComponent<TransformComponent>();
        ASSERT_TRUE(copyTransform);
        EXPECT_FLOAT_EQ(copyTransform->_x, kOriginalX);
        copyTransform->_x = kEditedX;
        copy->SetLayer(7);
        EXPECT_FLOAT_EQ(a.GetComponent<TransformComponent>()->_x, kOriginalX);
        EXPECT_EQ(a.GetLayer(), 0);
    }

    TEST_F(SceneTreeEditTest, TheNameOfADuplicateIsFreeAmongTheSiblings) {
        const IntrusivePtr<SceneNode> first = _scene->DuplicateNode(Node("A"));
        const IntrusivePtr<SceneNode> second = _scene->DuplicateNode(Node("A"));
        const IntrusivePtr<SceneNode> third = _scene->DuplicateNode(Node("A"));

        ASSERT_TRUE(first && second && third);
        EXPECT_EQ(first->GetName(), "A (copy)"_intern);
        EXPECT_EQ(second->GetName(), "A (copy 2)"_intern);
        EXPECT_EQ(third->GetName(), "A (copy 3)"_intern);
        // Each lands right behind the original
        EXPECT_EQ(Tree(), "A A (copy 3) A (copy 2) A (copy) B B1. B1a.. B2. C");
    }

    TEST_F(SceneTreeEditTest, TheRootAndForeignNodesCannotBeDuplicated) {
        Scene& other = *_manager->GetSceneByName("Other"_intern);
        const eastl::string before = Tree();

        EXPECT_FALSE(_scene->DuplicateNode(_scene->GetRootNode()));
        EXPECT_FALSE(_scene->DuplicateNode(*Find(other.GetRootNode(), "Foreign")));

        EXPECT_EQ(Tree(), before);
    }

    TEST_F(SceneTreeEditTest, ADuplicateKeepsTheSourceRefOfItsTopNode) {
        Node("A").SetSourceRef("nodes/Box"_intern);

        const IntrusivePtr<SceneNode> copy = _scene->DuplicateNode(Node("A"));

        ASSERT_TRUE(copy);
        EXPECT_EQ(copy->GetSourceRef(), "nodes/Box"_intern);
    }

    TEST_F(SceneTreeEditTest, UniqueSiblingNamesCountUpFromTwo) {
        SceneNode& b = Node("B");

        EXPECT_EQ(_scene->MakeUniqueSiblingName(b, "Fresh"), "Fresh"_intern);
        EXPECT_EQ(_scene->MakeUniqueSiblingName(b, "B1"), "B1 2"_intern);

        b.AddChild("B1 2"_intern);
        EXPECT_EQ(_scene->MakeUniqueSiblingName(b, "B1"), "B1 3"_intern);
        // A name used deeper in the tree is no sibling
        EXPECT_EQ(_scene->MakeUniqueSiblingName(b, "B1a"), "B1a"_intern);
    }

}  // namespace BECore::Tests
