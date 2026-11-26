#include <BECore/FakeControlEndpoint.h>
#include <BECore/Reflection/PropertyAccess.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Widgets/RemoteEditor/RemoteHierarchySource.h>
#include <Widgets/RemoteEditor/RemoteSession.h>
#include <Widgets/SceneEditor/HierarchyModel.h>
#include <Widgets/SceneEditor/HierarchyPanel.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using namespace HierarchyModel;

        // Root: A, B (B1 (B1a), B2), C. Components: A has a transform, B1a a quad, C a transform and a quad
        constexpr const char* kTreeNodes = R"(
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
</components></node>)";

        /// A session attached to a program whose scene is the tree above
        struct AttachedProgram {
            AttachedProgram() {
                EXPECT_TRUE(session.Attach("ws://127.0.0.1:8765").has_value());
                Exchange();
                EXPECT_EQ(session.GetState(), RemoteSessionState::Attached);
            }

            void Exchange() {
                session.Poll(now);
                endpoint.Serve();
                session.Poll(now);
            }

            FakeControlEndpoint endpoint{kTreeNodes};
            RemoteSession session{endpoint.Factory()};
            RemoteSessionTime now{};
        };

        constexpr const char* kTransform = "TransformComponent";
        constexpr const char* kQuad = "QuadRendererComponent";
        constexpr float kTopOfRow = 0.0f;
        constexpr float kJustInsideTheUpperQuarter = 0.24f;
        constexpr float kMiddleOfRow = 0.5f;
        constexpr float kJustBelowTheMiddle = 0.74f;
        constexpr float kBottomOfRow = 1.0f;

    }  // namespace

    /// The pure decisions of the hierarchy panel, on a scene loaded from a string
    class HierarchyModelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            XmlDeserializer deserializer;
            const String config = SceneConfigText(kTreeNodes);
            ASSERT_TRUE(deserializer.LoadFromString(config.c_str()));
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
            SceneNode* node = FindNodeBelow(_scene->GetRootNode(), name);
            EXPECT_NE(node, nullptr) << name;
            return Require(node);
        }

        SceneNode& Root() {
            return _scene->GetRootNode();
        }

        /// The names of the nodes that pass the filter, in tree order
        String Matches(StringView query, StringView type) {
            const SceneSelection none;
            HierarchyState state;
            return MatchesOf(LocalHierarchySource(*_scene, state, none), query, type);
        }

        static String MatchesOf(const IHierarchySource& source, StringView query, StringView type) {
            String out;
            Collect(source, source.GetRoot(), query, type, out);
            return out;
        }

        static void Collect(const IHierarchySource& source, HierarchyRow row, StringView query, StringView type, String& out) {
            for (size_t i = 0; i < source.GetChildCount(row); ++i) {
                const HierarchyRow child = source.GetChild(row, i);
                if (NodeMatchesFilter(source, child, query, type)) {
                    out.append(out.empty() ? "" : " ");
                    out.append(source.GetName(child));
                }
                Collect(source, child, query, type, out);
            }
        }

        bool SubtreeMatches(const char* name, StringView query, StringView type) {
            const SceneSelection none;
            HierarchyState state;
            return SubtreeMatchesFilter(LocalHierarchySource(*_scene, state, none), LocalHierarchySource::RowOf(Node(name)), query, type);
        }

        /// Every row below the root as `depth name icon`
        static Vector<String> Rows(const IHierarchySource& source) {
            Vector<String> out;
            AddRows(source, source.GetRoot(), 0, out);
            return out;
        }

        static void AddRows(const IHierarchySource& source, HierarchyRow row, size_t depth, Vector<String>& out) {
            out.push_back(Format("{} {} {}", depth, source.GetName(row), source.GetIcon(row)));
            for (size_t i = 0; i < source.GetChildCount(row); ++i) {
                AddRows(source, source.GetChild(row, i), depth + 1, out);
            }
        }

        /// Asks a row and what is below it for all the panel reads in a frame; how many rows there were
        static size_t Walk(const IHierarchySource& source, HierarchyRow row) {
            size_t rows = source.GetName(row) != nullptr && source.GetIcon(row) != nullptr && source.GetId(row) != nullptr ? 1 : 0;
            static_cast<void>(source.IsSelected(row));
            static_cast<void>(source.IsHidden(row));
            static_cast<void>(NodeMatchesFilter(source, row, "b", kQuad));
            for (size_t i = 0; i < source.GetChildCount(row); ++i) {
                rows += Walk(source, source.GetChild(row, i));
            }
            return rows;
        }

        /// Applies a drop that CanDrop allows with Scene::MoveNode and returns the tree as text
        String DropAndShow(const char* dragged, const char* target, DropPlace place) {
            EXPECT_TRUE(CanDrop(Node(dragged), Node(target), place));
            const DropTarget resolved = ResolveDrop(Node(dragged), Node(target), place);
            EXPECT_TRUE(_scene->MoveNode(Node(dragged), resolved.parent, resolved.index));
            String out;
            Show(Root(), 0, out);
            return out;
        }

        void Show(const SceneNode& node, size_t depth, String& out) {
            for (const auto& child : node.GetChildren()) {
                out.append(out.empty() ? "" : " ");
                out.append(child->GetName().CStr());
                out.append(depth, '.');
                Show(*child, depth + 1, out);
            }
        }

        IntrusivePtr<SceneManager> _manager;
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
        const SceneSelection none;
        HierarchyState state;
        const LocalHierarchySource source(*_scene, state, none);
        EXPECT_FALSE(NodeMatchesFilter(source, LocalHierarchySource::RowOf(Node("Branch")), "", kQuad));
        EXPECT_TRUE(SubtreeMatches("Branch", "", kQuad));
        EXPECT_TRUE(SubtreeMatches("B1", "", kQuad));
        EXPECT_TRUE(SubtreeMatches("Leaf", "", kQuad));
        EXPECT_FALSE(SubtreeMatches("B2", "", kQuad));
        EXPECT_FALSE(SubtreeMatches("A", "", kQuad));
        EXPECT_TRUE(SubtreeMatchesFilter(source, source.GetRoot(), "leaf", ""));
    }

    TEST_F(HierarchyModelTest, TheTreeOfAnotherProgramShowsAndFiltersAsTheLocalOne) {
        AttachedProgram program;
        const RemoteHierarchySource remote(program.session);
        const SceneSelection none;
        HierarchyState state;
        const LocalHierarchySource local(program.endpoint.GetScene(), state, none);

        EXPECT_EQ(Rows(remote), Rows(local));
        for (const StringView query : {"", "b", "LEAF", "root", "nothing"}) {
            for (const StringView type : {"", kTransform, kQuad}) {
                EXPECT_EQ(MatchesOf(remote, query, type), MatchesOf(local, query, type)) << query.data() << " / " << type.data();
                EXPECT_EQ(NodeMatchesFilter(remote, remote.GetRoot(), query, type), NodeMatchesFilter(local, local.GetRoot(), query, type)) << query.data();
                EXPECT_EQ(SubtreeMatchesFilter(remote, remote.GetRoot(), query, type), SubtreeMatchesFilter(local, local.GetRoot(), query, type)) << query.data();
            }
        }
        Vector<String> remoteTypes;
        Vector<String> localTypes;
        remote.CollectComponentTypes(remoteTypes);
        local.CollectComponentTypes(localTypes);
        EXPECT_EQ(remoteTypes, localTypes);
        EXPECT_FALSE(remote.IsTruncated());
    }

    TEST_F(HierarchyModelTest, AClickOnARowOfAnotherProgramSelectsInTheSession) {
        AttachedProgram program;
        RemoteHierarchySource source(program.session);
        const HierarchyRow root = source.GetRoot();
        const HierarchyRow branch = source.GetChild(root, 1);
        const HierarchyRow inner = source.GetChild(branch, 0);
        EXPECT_EQ(source.EditableNode(root), nullptr);
        EXPECT_EQ(source.EditableNode(inner), nullptr);

        source.Click(inner, kSelectionModifierCtrl);
        EXPECT_EQ(program.session.GetSelection(), Optional<String>(String("Branch/B1")));
        EXPECT_TRUE(source.IsSelected(inner));
        EXPECT_FALSE(source.IsSelected(branch));
        EXPECT_FALSE(source.IsSelected(root));

        source.Click(root, 0);
        EXPECT_FALSE(program.session.GetSelection().has_value());
        EXPECT_FALSE(source.IsSelected(inner));
    }

    TEST_F(HierarchyModelTest, OnlyTheRowsAboveTheRevealedNodeOfAnotherProgramAreOnItsPath) {
        AttachedProgram program;
        // A name that begins the name of a node on the path
        program.endpoint.Node("A").SetName("Bran"_intern);
        program.now += RemoteSessionTiming::kTreeInterval;
        program.Exchange();
        RemoteHierarchySource source(program.session);
        const HierarchyRow root = source.GetRoot();
        const HierarchyRow namesake = source.GetChild(root, 0);
        const HierarchyRow branch = source.GetChild(root, 1);
        const HierarchyRow inner = source.GetChild(branch, 0);
        EXPECT_FALSE(source.IsOnRevealPath(root));
        EXPECT_FALSE(source.Reveal("Branch/B1/Gone").has_value());

        ASSERT_TRUE(source.Reveal("Branch/B1/Leaf").has_value());
        EXPECT_TRUE(source.IsOnRevealPath(root));
        EXPECT_TRUE(source.IsOnRevealPath(branch));
        EXPECT_TRUE(source.IsOnRevealPath(inner));
        EXPECT_FALSE(source.IsOnRevealPath(namesake));
        EXPECT_FALSE(source.IsOnRevealPath(source.GetChild(root, 2)));
        const HierarchyRow leaf = source.GetChild(inner, 0);
        EXPECT_TRUE(source.IsRevealTarget(leaf));
        EXPECT_FALSE(source.IsRevealTarget(inner));
        EXPECT_FALSE(source.IsOnRevealPath(leaf));
    }

    TEST_F(HierarchyModelTest, ARowOfAnotherProgramKeepsItsIdOverANewReadOfTheTree) {
        AttachedProgram program;
        const RemoteHierarchySource source(program.session);
        const HierarchyRow root = source.GetRoot();
        const void* const rootId = source.GetId(root);
        const void* const first = source.GetId(source.GetChild(root, 0));
        const void* const last = source.GetId(source.GetChild(root, 2));
        EXPECT_NE(first, last);
        EXPECT_NE(first, rootId);

        // Another text, so the tree is built again
        program.endpoint.Node("A").SetLayer(5);
        program.now += RemoteSessionTiming::kTreeInterval;
        program.Exchange();
        ASSERT_EQ(Require(program.session.GetTree().Find("A")).layer, 5);

        EXPECT_EQ(source.GetId(source.GetRoot()), rootId);
        EXPECT_EQ(source.GetId(source.GetChild(root, 0)), first);
        EXPECT_EQ(source.GetId(source.GetChild(root, 2)), last);
    }

    TEST_F(HierarchyModelTest, AWalkOfTheTreeOfAnotherProgramAllocatesNothing) {
        AttachedProgram program;
        program.session.Select("Branch/B1");
        const RemoteHierarchySource source(program.session);
        size_t seen = Walk(source, source.GetRoot());  // an icon's first lookup may set itself up

        ExpectNoAllocations([&] { seen = Walk(source, source.GetRoot()); });
        EXPECT_GT(seen, 0u);
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
        EXPECT_TRUE(ValidateName(String(kMaxNodeNameLength, 'x')));
        EXPECT_FALSE(ValidateName(String(kMaxNodeNameLength + 1, 'x')));
    }

    TEST_F(HierarchyModelTest, TheComponentTypesAreSortedWithoutRepeats) {
        Vector<StringView> types;
        types.push_back("Stale");
        CollectComponentTypes(Root(), types);

        ASSERT_EQ(types.size(), 2u);
        EXPECT_EQ(types[0], kQuad);
        EXPECT_EQ(types[1], kTransform);
    }

    TEST_F(HierarchyModelTest, CollectingKeepsTheCapacityOfTheVector) {
        Vector<StringView> types;
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
        EXPECT_EQ(FindNodeBelow(Root(), "Leaf"), nullptr);
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
        EXPECT_NE(FindNodeBelow(Root(), "Leaf"), nullptr);
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
        ASSERT_TRUE(deserializer.LoadFromString(SceneConfigText(kTreeNodes).c_str()));
        other->Deserialize(deserializer);
        other->Initialize();
        Scene* otherScene = other->GetSceneByName("Main"_intern);
        ASSERT_NE(otherScene, nullptr);

        EXPECT_FALSE(ApplyOperation(*_scene, {OperationKind::Delete, FindNodeBelow(otherScene->GetRootNode(), "A"), nullptr, 0}).changed);
        EXPECT_NE(FindNodeBelow(otherScene->GetRootNode(), "A"), nullptr);
    }

    TEST_F(HierarchyModelTest, AncestorsOfListsTheRowsToOpenFromTheRootDown) {
        Vector<SceneNode*> ancestors;
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

    TEST_F(HierarchyModelTest, ResolveLiveKeepsTheLiveNodesInTheirOrderAndDropsDeadForeignAndNullOnes) {
        SceneNode* const removed = &Node("B2");
        const Vector<SceneNode*> wanted{&Node("C"), removed, &Root(), nullptr, &Node("Leaf"), &Node("A")};
        Node("Branch").RemoveChild(removed);

        Vector<SceneNode*> live;
        HashSet<const SceneNode*> scratch;
        ResolveLive(Root(), {wanted.data(), wanted.size()}, live, scratch);

        // The root counts as part of its own tree, the removed node (never dereferenced) and the null pointer are dropped
        ASSERT_EQ(live.size(), 4u);
        EXPECT_EQ(live[0], &Node("C"));
        EXPECT_EQ(live[1], &Root());
        EXPECT_EQ(live[2], &Node("Leaf"));
        EXPECT_EQ(live[3], &Node("A"));
    }

    TEST_F(HierarchyModelTest, ResolveLiveReusesItsListAndItsScratch) {
        Vector<SceneNode*> live;
        HashSet<const SceneNode*> scratch;
        const Vector<SceneNode*> first{&Node("A"), &Node("B1"), &Node("C")};
        ResolveLive(Root(), {first.data(), first.size()}, live, scratch);
        const size_t capacity = live.capacity();
        ASSERT_EQ(live.size(), 3u);

        const Vector<SceneNode*> second{&Node("Leaf")};
        ResolveLive(Root(), {second.data(), second.size()}, live, scratch);

        ASSERT_EQ(live.size(), 1u);
        EXPECT_EQ(live[0], &Node("Leaf"));
        EXPECT_EQ(live.capacity(), capacity);

        ResolveLive(Root(), {}, live, scratch);
        EXPECT_TRUE(live.empty());
    }

    TEST_F(HierarchyModelTest, ResolveLiveDropsANodeOfAnotherTree) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(R"(<SceneManager><scenes><scene type="Scene" name="Second"><nodes><node name="Stranger"/></nodes></scene></scenes></SceneManager>)"));
        auto other = New<SceneManager>();
        other->Deserialize(deserializer);
        other->Initialize();
        SceneNode* const stranger = FindNodeBelow(other->GetSceneByName("Second"_intern)->GetRootNode(), "Stranger");
        const Vector<SceneNode*> wanted{stranger, &Node("A")};

        Vector<SceneNode*> live;
        HashSet<const SceneNode*> scratch;
        ResolveLive(Root(), {wanted.data(), wanted.size()}, live, scratch);

        ASSERT_EQ(live.size(), 1u);
        EXPECT_EQ(live[0], &Node("A"));
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

        state.componentType = kTransform;
        snprintf(state.searchBuffer, sizeof(state.searchBuffer), "a");
        ASSERT_TRUE(HierarchyPanel::Reveal(*_scene, state, _scene, &Node("A")));
        EXPECT_STREQ(state.searchBuffer, "a");
        EXPECT_EQ(state.componentType, kTransform);
    }

    TEST_F(HierarchyModelTest, RevealIgnoresAnotherSceneANodeNotInTheTreeAndNull) {
        auto other = New<SceneManager>();
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(SceneConfigText(kTreeNodes).c_str()));
        other->Deserialize(deserializer);
        other->Initialize();
        Scene* otherScene = other->GetSceneByName("Main"_intern);
        ASSERT_NE(otherScene, nullptr);
        SceneNode* removed = &Node("B2");
        HierarchyState state;
        state.selected = &Node("A");

        // The node of another scene, asked for with its own scene or with ours
        EXPECT_FALSE(HierarchyPanel::Reveal(*_scene, state, otherScene, FindNodeBelow(otherScene->GetRootNode(), "A")));
        EXPECT_FALSE(HierarchyPanel::Reveal(*_scene, state, _scene, FindNodeBelow(otherScene->GetRootNode(), "A")));
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
