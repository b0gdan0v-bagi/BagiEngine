#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/SceneEditor/HierarchyModel.h>
#include <Widgets/SceneEditor/HierarchyPanel.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using namespace HierarchyModel;

        // Root: A, B (B1 (B1a), B2), C. Components: A has a transform, B1a a quad, C a transform and a quad
        constexpr const char* kTreeScene = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="A"><components><component type="TransformComponent"/></components></node>
    <node name="Branch"><children>
      <node name="B1"><children>
        <node name="Leaf"><components><component type="QuadRendererComponent"/></components></node>
      </children></node>
      <node name="B2"/>
    </children></node>
    <node name="C"><components>
      <component type="TransformComponent"/>
      <component type="QuadRendererComponent"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr const char* kTransform = "TransformComponent";
        constexpr const char* kQuad = "QuadRendererComponent";
        constexpr float kTopOfRow = 0.0f;
        constexpr float kJustInsideTheUpperQuarter = 0.24f;
        constexpr float kMiddleOfRow = 0.5f;
        constexpr float kJustBelowTheMiddle = 0.74f;
        constexpr float kBottomOfRow = 1.0f;

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

    /// The pure decisions of the hierarchy panel, on a scene loaded from a string
    class HierarchyModelTest : public ::testing::Test {
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

        SceneNode& Root() {
            return _scene->GetRootNode();
        }

        /// The names of the nodes that pass the filter, in tree order
        eastl::string Matches(eastl::string_view query, eastl::string_view type) {
            eastl::string out;
            Collect(Root(), query, type, out);
            return out;
        }

        void Collect(const SceneNode& node, eastl::string_view query, eastl::string_view type, eastl::string& out) {
            for (const auto& child : node.GetChildren()) {
                if (NodeMatchesFilter(*child, query, type)) {
                    out.append(out.empty() ? "" : " ");
                    out.append(child->GetName().CStr());
                }
                Collect(*child, query, type, out);
            }
        }

        /// Applies a drop that CanDrop allows with Scene::MoveNode and returns the tree as text
        eastl::string DropAndShow(const char* dragged, const char* target, DropPlace place) {
            EXPECT_TRUE(CanDrop(Node(dragged), Node(target), place));
            const DropTarget resolved = ResolveDrop(Node(dragged), Node(target), place);
            EXPECT_TRUE(_scene->MoveNode(Node(dragged), resolved.parent, resolved.index));
            eastl::string out;
            Show(Root(), 0, out);
            return out;
        }

        void Show(const SceneNode& node, size_t depth, eastl::string& out) {
            for (const auto& child : node.GetChildren()) {
                out.append(out.empty() ? "" : " ");
                out.append(child->GetName().CStr());
                out.append(depth, '.');
                Show(*child, depth + 1, out);
            }
        }

        IntrusivePtrAtomic<SceneManager> _manager;
        Scene* _scene = nullptr;
    };

    TEST_F(HierarchyModelTest, AnEmptyFilterKeepsEveryNode) {
        EXPECT_FALSE(IsFilterActive("", ""));
        EXPECT_TRUE(IsFilterActive("a", ""));
        EXPECT_TRUE(IsFilterActive("", kQuad));
        EXPECT_EQ(Matches("", ""), "A Branch B1 Leaf B2 C");
    }

    TEST_F(HierarchyModelTest, TheSearchIsASubstringOfTheNameWithoutCase) {
        EXPECT_EQ(Matches("b", ""), "Branch B1 B2");
        EXPECT_EQ(Matches("BRAN", ""), "Branch");
        EXPECT_EQ(Matches("ea", ""), "Leaf");
        EXPECT_EQ(Matches("nothing", ""), "");
        // A query longer than every name
        EXPECT_EQ(Matches("BranchBranch", ""), "");
    }

    TEST_F(HierarchyModelTest, TheTypeFilterKeepsNodesWithThatComponent) {
        EXPECT_EQ(Matches("", kTransform), "A C");
        EXPECT_EQ(Matches("", kQuad), "Leaf C");
        EXPECT_EQ(Matches("", "NoSuchComponent"), "");
    }

    TEST_F(HierarchyModelTest, TheSearchAndTheTypeBothHaveToHold) {
        EXPECT_EQ(Matches("c", kQuad), "C");
        EXPECT_EQ(Matches("a", kTransform), "A");
        EXPECT_EQ(Matches("B1", kQuad), "");
    }

    TEST_F(HierarchyModelTest, AnAncestorOfAMatchStaysInTheTree) {
        EXPECT_FALSE(NodeMatchesFilter(Node("Branch"), "", kQuad));
        EXPECT_TRUE(SubtreeMatchesFilter(Node("Branch"), "", kQuad));
        EXPECT_TRUE(SubtreeMatchesFilter(Node("B1"), "", kQuad));
        EXPECT_TRUE(SubtreeMatchesFilter(Node("Leaf"), "", kQuad));
        EXPECT_FALSE(SubtreeMatchesFilter(Node("B2"), "", kQuad));
        EXPECT_FALSE(SubtreeMatchesFilter(Node("A"), "", kQuad));
        EXPECT_TRUE(SubtreeMatchesFilter(Root(), "leaf", ""));
    }

    TEST(HierarchyModelPlaceTest, TheQuartersOfARowGiveThePlace) {
        EXPECT_EQ(ClassifyDropPlace(kTopOfRow), DropPlace::Before);
        EXPECT_EQ(ClassifyDropPlace(kJustInsideTheUpperQuarter), DropPlace::Before);
        EXPECT_EQ(ClassifyDropPlace(0.25f), DropPlace::Into);
        EXPECT_EQ(ClassifyDropPlace(kMiddleOfRow), DropPlace::Into);
        EXPECT_EQ(ClassifyDropPlace(kJustBelowTheMiddle), DropPlace::Into);
        EXPECT_EQ(ClassifyDropPlace(0.75f), DropPlace::After);
        EXPECT_EQ(ClassifyDropPlace(kBottomOfRow), DropPlace::After);
    }

    TEST_F(HierarchyModelTest, TheRootCannotBeDragged) {
        for (const DropPlace place : {DropPlace::Before, DropPlace::Into, DropPlace::After}) {
            EXPECT_FALSE(CanDrop(Root(), Node("A"), place));
        }
    }

    TEST_F(HierarchyModelTest, ANodeCannotGoOntoItselfOrIntoItsOwnSubtree) {
        for (const DropPlace place : {DropPlace::Before, DropPlace::Into, DropPlace::After}) {
            EXPECT_FALSE(CanDrop(Node("Branch"), Node("Branch"), place));
            EXPECT_FALSE(CanDrop(Node("Branch"), Node("B1"), place));
            EXPECT_FALSE(CanDrop(Node("Branch"), Node("Leaf"), place));
        }
    }

    TEST_F(HierarchyModelTest, NothingGoesBeforeOrAfterTheRoot) {
        EXPECT_FALSE(CanDrop(Node("A"), Root(), DropPlace::Before));
        EXPECT_FALSE(CanDrop(Node("A"), Root(), DropPlace::After));
        EXPECT_TRUE(CanDrop(Node("B1"), Root(), DropPlace::Into));
    }

    TEST_F(HierarchyModelTest, ADropThatLeavesTheNodeWhereItIsIsRefused) {
        // Right after the one above it, right before the one below it, and on the node itself
        EXPECT_FALSE(CanDrop(Node("Branch"), Node("A"), DropPlace::After));
        EXPECT_FALSE(CanDrop(Node("Branch"), Node("C"), DropPlace::Before));
        // Into the parent it already ends, when it is the last child
        EXPECT_FALSE(CanDrop(Node("C"), Root(), DropPlace::Into));
        EXPECT_FALSE(CanDrop(Node("B2"), Node("Branch"), DropPlace::Into));
        // The same moves a step away are fine
        EXPECT_TRUE(CanDrop(Node("Branch"), Node("A"), DropPlace::Before));
        EXPECT_TRUE(CanDrop(Node("Branch"), Node("C"), DropPlace::After));
        EXPECT_TRUE(CanDrop(Node("B1"), Node("Branch"), DropPlace::Into));
    }

    TEST_F(HierarchyModelTest, ALockedNodeCannotBeDraggedOrReceiveADrop) {
        Node("A").SetEditorFlags(EditorFlags::Locked);
        EXPECT_FALSE(CanDrop(Node("A"), Node("C"), DropPlace::After));

        Node("Branch").SetEditorFlags(EditorFlags::Locked);
        EXPECT_FALSE(CanDrop(Node("C"), Node("Branch"), DropPlace::Into));
        // A locked parent refuses a drop beside its children too
        EXPECT_FALSE(CanDrop(Node("C"), Node("B1"), DropPlace::Before));
        EXPECT_FALSE(CanDrop(Node("C"), Node("B2"), DropPlace::After));
        // A locked node as the neighbour at the same level is no obstacle: the new parent is the root
        EXPECT_TRUE(CanDrop(Node("C"), Node("Branch"), DropPlace::Before));
        // Only the node's own lock counts
        EXPECT_TRUE(CanDrop(Node("C"), Node("Leaf"), DropPlace::Into));
        // Hidden does not matter
        Node("C").SetEditorFlags(EditorFlags::Hidden);
        EXPECT_TRUE(CanDrop(Node("C"), Node("A"), DropPlace::Before));
    }

    TEST_F(HierarchyModelTest, IntoTheTargetMakesTheNodeItsLastChild) {
        EXPECT_EQ(DropAndShow("A", "Branch", DropPlace::Into), "Branch B1. Leaf.. B2. A. C");
    }

    TEST_F(HierarchyModelTest, BeforeAndAfterPlaceTheNodeBesideTheTarget) {
        EXPECT_EQ(DropAndShow("C", "B1", DropPlace::Before), "A Branch C. B1. Leaf.. B2.");
        EXPECT_EQ(DropAndShow("C", "B2", DropPlace::After), "A Branch B1. Leaf.. B2. C.");
    }

    TEST_F(HierarchyModelTest, ADropInsideOneParentCountsAfterTheNodeLeft) {
        // Down: A goes behind Branch, which is the index of Branch after A left
        EXPECT_EQ(DropAndShow("A", "Branch", DropPlace::After), "Branch B1. Leaf.. B2. A C");
        EXPECT_EQ(DropAndShow("A", "C", DropPlace::After), "Branch B1. Leaf.. B2. C A");
        // Up
        EXPECT_EQ(DropAndShow("A", "Branch", DropPlace::Before), "A Branch B1. Leaf.. B2. C");
        // Down but not to the end
        EXPECT_EQ(DropAndShow("A", "C", DropPlace::Before), "Branch B1. Leaf.. B2. A C");
    }

    TEST_F(HierarchyModelTest, IntoTheOwnParentGoesToTheEnd) {
        EXPECT_EQ(DropAndShow("B1", "Branch", DropPlace::Into), "A Branch B2. B1. Leaf.. C");
    }

    TEST_F(HierarchyModelTest, TheResolvedParentOfBeforeAndAfterIsTheParentOfTheTarget) {
        EXPECT_EQ(ResolveDrop(Node("A"), Node("B2"), DropPlace::Before).parent, &Node("Branch"));
        EXPECT_EQ(ResolveDrop(Node("A"), Node("B2"), DropPlace::Before).index, 1u);
        EXPECT_EQ(ResolveDrop(Node("A"), Node("B2"), DropPlace::After).index, 2u);
        EXPECT_EQ(ResolveDrop(Node("A"), Node("Branch"), DropPlace::Into).parent, &Node("Branch"));
        EXPECT_EQ(ResolveDrop(Node("Leaf"), Root(), DropPlace::Into).parent, &Root());
        EXPECT_EQ(ResolveDrop(Node("Leaf"), Root(), DropPlace::Into).index, 3u);
    }

    TEST(HierarchyModelNameTest, ANameNeedsASignAndABoundedLength) {
        EXPECT_TRUE(ValidateName("A"));
        EXPECT_TRUE(ValidateName("  two words  "));
        EXPECT_FALSE(ValidateName(""));
        EXPECT_FALSE(ValidateName("   \t "));
        EXPECT_TRUE(ValidateName(eastl::string(kMaxNodeNameLength, 'x')));
        EXPECT_FALSE(ValidateName(eastl::string(kMaxNodeNameLength + 1, 'x')));
    }

    TEST_F(HierarchyModelTest, TheComponentTypesAreSortedWithoutRepeats) {
        eastl::vector<eastl::string_view> types;
        types.push_back("Stale");
        CollectComponentTypes(Root(), types);

        ASSERT_EQ(types.size(), 2u);
        EXPECT_EQ(types[0], kQuad);
        EXPECT_EQ(types[1], kTransform);
    }

    TEST_F(HierarchyModelTest, CollectingKeepsTheCapacityOfTheVector) {
        eastl::vector<eastl::string_view> types;
        types.reserve(16);
        const auto* data = types.data();
        CollectComponentTypes(Root(), types);
        CollectComponentTypes(Node("Branch"), types);

        EXPECT_EQ(types.data(), data);
        ASSERT_EQ(types.size(), 1u);
        EXPECT_EQ(types[0], kQuad);
    }

    TEST_F(HierarchyModelTest, DeleteRemovesTheSubtreeAndRevealsTheParent) {
        SceneNode& branch = Node("Branch");
        const OperationResult result = ApplyOperation(*_scene, {OperationKind::Delete, &Node("B1"), nullptr, 0});

        EXPECT_TRUE(result.changed);
        EXPECT_EQ(result.reveal, &branch);
        EXPECT_EQ(Find(Root(), "Leaf"), nullptr);
        EXPECT_EQ(branch.GetChildren().size(), 1u);
    }

    TEST_F(HierarchyModelTest, DuplicateAddsACopyRightAfterAndReportsIt) {
        const OperationResult result = ApplyOperation(*_scene, {OperationKind::Duplicate, &Node("A"), nullptr, 0});

        ASSERT_TRUE(result.changed);
        ASSERT_NE(result.created, nullptr);
        EXPECT_EQ(result.created->GetName(), "A (copy)"_intern);
        EXPECT_EQ(result.reveal, &Root());
        EXPECT_EQ(Root().GetChildren()[1].Get(), result.created);
    }

    TEST_F(HierarchyModelTest, MoveGoesThroughTheScene) {
        SceneNode& branch = Node("Branch");
        const OperationResult result = ApplyOperation(*_scene, {OperationKind::Move, &Node("A"), &branch, 0});

        EXPECT_TRUE(result.changed);
        EXPECT_EQ(result.reveal, &branch);
        EXPECT_EQ(Node("A").GetParent(), &branch);
        EXPECT_EQ(branch.GetChildren()[0].Get(), &Node("A"));
    }

    TEST_F(HierarchyModelTest, ARefusedOperationChangesNothing) {
        EXPECT_FALSE(ApplyOperation(*_scene, {OperationKind::Delete, &Root(), nullptr, 0}).changed);
        EXPECT_FALSE(ApplyOperation(*_scene, {OperationKind::Duplicate, &Root(), nullptr, 0}).changed);
        EXPECT_FALSE(ApplyOperation(*_scene, {OperationKind::Move, &Node("Branch"), &Node("B1"), 0}).changed);
        EXPECT_FALSE(ApplyOperation(*_scene, {OperationKind::None, &Node("A"), nullptr, 0}).changed);
        EXPECT_FALSE(ApplyOperation(*_scene, {OperationKind::Delete, nullptr, nullptr, 0}).changed);
        EXPECT_EQ(Root().GetChildren().size(), 3u);
        EXPECT_NE(Find(Root(), "Leaf"), nullptr);
    }

    TEST_F(HierarchyModelTest, ALockedNodeIsNotDeletedOrMovedButMayBeDuplicated) {
        Node("A").SetEditorFlags(EditorFlags::Locked);

        EXPECT_FALSE(ApplyOperation(*_scene, {OperationKind::Delete, &Node("A"), nullptr, 0}).changed);
        EXPECT_FALSE(ApplyOperation(*_scene, {OperationKind::Move, &Node("A"), &Node("Branch"), 0}).changed);
        EXPECT_EQ(Root().GetChildren().size(), 3u);

        const OperationResult copy = ApplyOperation(*_scene, {OperationKind::Duplicate, &Node("A"), nullptr, 0});
        ASSERT_TRUE(copy.changed);
        EXPECT_EQ(copy.created->GetEditorFlags(), EditorFlags::None);
    }

    TEST_F(HierarchyModelTest, NothingIsMovedIntoALockedParent) {
        Node("Branch").SetEditorFlags(EditorFlags::Locked);

        EXPECT_FALSE(ApplyOperation(*_scene, {OperationKind::Move, &Node("A"), &Node("Branch"), 0}).changed);
        EXPECT_EQ(Node("A").GetParent(), &Root());
        // The lock of a parent does not stop its children from being deleted: only the node's own lock counts
        EXPECT_TRUE(ApplyOperation(*_scene, {OperationKind::Delete, &Node("B2"), nullptr, 0}).changed);
    }

    TEST_F(HierarchyModelTest, TheSceneManagerClearsTheEditorFlagsOfEveryScene) {
        Node("A").SetEditorFlags(EditorFlags::Hidden);
        Root().SetEditorFlags(EditorFlags::Locked);

        _manager->ClearEditorFlags();

        EXPECT_EQ(Node("A").GetEditorFlags(), EditorFlags::None);
        EXPECT_EQ(Root().GetEditorFlags(), EditorFlags::None);
    }

    TEST_F(HierarchyModelTest, ANodeOfAnotherSceneIsRefused) {
        auto other = New<SceneManager>();
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(kTreeScene));
        other->Deserialize(deserializer);
        other->Initialize();
        Scene* otherScene = other->GetSceneByName("Main"_intern);
        ASSERT_NE(otherScene, nullptr);

        EXPECT_FALSE(ApplyOperation(*_scene, {OperationKind::Delete, Find(otherScene->GetRootNode(), "A"), nullptr, 0}).changed);
        EXPECT_NE(Find(otherScene->GetRootNode(), "A"), nullptr);
    }

    TEST_F(HierarchyModelTest, AncestorsOfListsTheRowsToOpenFromTheRootDown) {
        eastl::vector<SceneNode*> ancestors;
        ancestors.push_back(&Node("A"));

        AncestorsOf(Node("Leaf"), ancestors);

        ASSERT_EQ(ancestors.size(), 3u);
        EXPECT_EQ(ancestors[0], &Root());
        EXPECT_EQ(ancestors[1], &Node("Branch"));
        EXPECT_EQ(ancestors[2], &Node("B1"));

        AncestorsOf(Root(), ancestors);
        EXPECT_TRUE(ancestors.empty());
    }

    TEST_F(HierarchyModelTest, FindNodeComparesPointersAndGivesTheLiveNode) {
        EXPECT_EQ(FindNode(Root(), &Node("Leaf")), &Node("Leaf"));
        EXPECT_EQ(FindNode(Root(), &Root()), &Root());
        EXPECT_EQ(FindNode(Root(), nullptr), nullptr);
        EXPECT_EQ(FindNode(Node("Branch"), &Node("A")), nullptr);

        SceneNode* removed = &Node("B2");
        Node("Branch").RemoveChild(removed);
        EXPECT_EQ(FindNode(Root(), removed), nullptr);
    }

    TEST_F(HierarchyModelTest, TheSelectionIsAnnouncedOncePerChange) {
        SelectionTracker tracker;

        EXPECT_FALSE(tracker.Changed(nullptr, nullptr));
        EXPECT_TRUE(tracker.Changed(_scene, nullptr));
        EXPECT_FALSE(tracker.Changed(_scene, nullptr));
        EXPECT_TRUE(tracker.Changed(_scene, &Node("A")));
        EXPECT_FALSE(tracker.Changed(_scene, &Node("A")));
        EXPECT_TRUE(tracker.Changed(_scene, nullptr));
        EXPECT_TRUE(tracker.Changed(nullptr, nullptr));
    }

    TEST_F(HierarchyModelTest, RevealSelectsTheNodeAndPreparesTheNextWalk) {
        HierarchyState state;

        ASSERT_TRUE(HierarchyPanel::Reveal(*_scene, state, _scene, &Node("Leaf")));

        EXPECT_EQ(state.selected, &Node("Leaf"));
        EXPECT_EQ(state.scrollTo, &Node("Leaf"));
        ASSERT_EQ(state.openNodes.size(), 3u);
        EXPECT_EQ(state.openNodes[2], &Node("B1"));
    }

    TEST_F(HierarchyModelTest, RevealClearsAFilterThatHidesTheNodeAndKeepsOneThatDoesNot) {
        HierarchyState state;
        state.componentType = kTransform;
        snprintf(state.searchBuffer, sizeof(state.searchBuffer), "a");

        // "Leaf" has no transform and no "a" in its subtree: the filter hides it
        ASSERT_TRUE(HierarchyPanel::Reveal(*_scene, state, _scene, &Node("Leaf")));
        EXPECT_EQ(state.searchBuffer[0], '\0');
        EXPECT_TRUE(state.componentType.empty());

        // "A" passes the filter
        state.componentType = kTransform;
        snprintf(state.searchBuffer, sizeof(state.searchBuffer), "a");
        ASSERT_TRUE(HierarchyPanel::Reveal(*_scene, state, _scene, &Node("A")));
        EXPECT_STREQ(state.searchBuffer, "a");
        EXPECT_EQ(state.componentType, kTransform);
    }

    TEST_F(HierarchyModelTest, RevealIgnoresAnotherSceneANodeNotInTheTreeAndNull) {
        auto other = New<SceneManager>();
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(kTreeScene));
        other->Deserialize(deserializer);
        other->Initialize();
        Scene* otherScene = other->GetSceneByName("Main"_intern);
        ASSERT_NE(otherScene, nullptr);
        SceneNode* removed = &Node("B2");
        HierarchyState state;
        state.selected = &Node("A");

        // The node of another scene, asked for with its own scene or with ours
        EXPECT_FALSE(HierarchyPanel::Reveal(*_scene, state, otherScene, Find(otherScene->GetRootNode(), "A")));
        EXPECT_FALSE(HierarchyPanel::Reveal(*_scene, state, _scene, Find(otherScene->GetRootNode(), "A")));
        EXPECT_FALSE(HierarchyPanel::Reveal(*_scene, state, _scene, nullptr));
        Node("Branch").RemoveChild(removed);
        EXPECT_FALSE(HierarchyPanel::Reveal(*_scene, state, _scene, removed));

        EXPECT_EQ(state.selected, &Node("A"));
        EXPECT_EQ(state.scrollTo, nullptr);
        EXPECT_TRUE(state.openNodes.empty());
    }

    TEST_F(HierarchyModelTest, ALockedNodeCanBeRevealed) {
        Node("A").SetEditorFlags(EditorFlags::Locked);
        HierarchyState state;

        EXPECT_TRUE(HierarchyPanel::Reveal(*_scene, state, _scene, &Node("A")));
        EXPECT_EQ(state.selected, &Node("A"));
    }

    TEST_F(HierarchyModelTest, SelfOrAncestorOf) {
        EXPECT_TRUE(IsSelfOrAncestorOf(&Node("Branch"), &Node("Leaf")));
        EXPECT_TRUE(IsSelfOrAncestorOf(&Node("Leaf"), &Node("Leaf")));
        EXPECT_FALSE(IsSelfOrAncestorOf(&Node("Leaf"), &Node("Branch")));
        EXPECT_FALSE(IsSelfOrAncestorOf(nullptr, &Node("Leaf")));
        EXPECT_FALSE(IsSelfOrAncestorOf(&Node("Leaf"), nullptr));
    }

    TEST_F(HierarchyModelTest, ContainsNodeComparesPointersOnly) {
        EXPECT_TRUE(ContainsNode(Root(), &Node("Leaf")));
        EXPECT_TRUE(ContainsNode(Root(), &Root()));
        EXPECT_FALSE(ContainsNode(Node("Branch"), &Node("A")));
        EXPECT_FALSE(ContainsNode(Root(), nullptr));

        SceneNode* removed = &Node("B2");
        Node("Branch").RemoveChild(removed);
        EXPECT_FALSE(ContainsNode(Root(), removed));
    }

}  // namespace BECore::Tests
