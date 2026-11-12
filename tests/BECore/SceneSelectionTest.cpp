#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/SceneEditor/HierarchyModel.h>
#include <Widgets/SceneEditor/SceneSelection.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Root: A, B (B1), C
        constexpr const char* kSelectionScene = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="A"/>
    <node name="B"><children><node name="B1"/></children></node>
    <node name="C"/>
  </nodes></scene>
  <scene type="Scene" name="Other"><nodes>
    <node name="Elsewhere"/>
  </nodes></scene>
</scenes></SceneManager>)";

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

    /// The selection set of the scene editor and the click rules, on nodes of a scene loaded from a string
    class SceneSelectionTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kSelectionScene));
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

        SceneNode* Node(const char* name) {
            SceneNode* node = Find(_scene->GetRootNode(), name);
            EXPECT_NE(node, nullptr) << name;
            return node;
        }

        void ExpectSelection(std::initializer_list<const char*> names) {
            ASSERT_EQ(_selection.Count(), names.size());
            size_t index = 0;
            for (const char* name : names) {
                EXPECT_EQ(_selection.Nodes()[index++], Node(name)) << name;
            }
        }

        IntrusivePtrAtomic<SceneManager> _manager;
        Scene* _scene = nullptr;
        SceneSelection _selection;
    };

    TEST_F(SceneSelectionTest, ItStartsEmpty) {
        EXPECT_EQ(_selection.Count(), 0u);
        EXPECT_EQ(_selection.Primary(), nullptr);
        EXPECT_TRUE(_selection.Nodes().empty());
        EXPECT_FALSE(_selection.Contains(Node("A")));
    }

    TEST_F(SceneSelectionTest, SetReplacesTheSelectionAndTheNodeIsThePrimary) {
        EXPECT_TRUE(_selection.Add(Node("A")));
        EXPECT_TRUE(_selection.Add(Node("B")));

        EXPECT_TRUE(_selection.Set(Node("C")));

        ExpectSelection({"C"});
        EXPECT_EQ(_selection.Primary(), Node("C"));
    }

    TEST_F(SceneSelectionTest, SetOfTheOnlySelectedNodeChangesNothingAndSetOfNullClears) {
        EXPECT_TRUE(_selection.Set(Node("A")));
        EXPECT_FALSE(_selection.Set(Node("A")));

        EXPECT_TRUE(_selection.Set(nullptr));
        EXPECT_EQ(_selection.Count(), 0u);
        EXPECT_FALSE(_selection.Set(nullptr));
    }

    TEST_F(SceneSelectionTest, SetOfANodeInAGroupCollapsesTheGroupToIt) {
        _selection.Add(Node("A"));
        _selection.Add(Node("B"));

        EXPECT_TRUE(_selection.Set(Node("B")));

        ExpectSelection({"B"});
    }

    TEST_F(SceneSelectionTest, AddKeepsTheOrderAndTheLastIsThePrimary) {
        _selection.Add(Node("B"));
        _selection.Add(Node("A"));
        _selection.Add(Node("C"));

        ExpectSelection({"B", "A", "C"});
        EXPECT_EQ(_selection.Primary(), Node("C"));
    }

    TEST_F(SceneSelectionTest, AddOfASelectedNodeOrNullChangesNothing) {
        _selection.Add(Node("A"));
        _selection.Add(Node("B"));

        EXPECT_FALSE(_selection.Add(Node("A")));
        EXPECT_FALSE(_selection.Add(nullptr));

        ExpectSelection({"A", "B"});
        EXPECT_EQ(_selection.Primary(), Node("B"));
    }

    TEST_F(SceneSelectionTest, ToggleAddsAndRemoves) {
        EXPECT_TRUE(_selection.Toggle(Node("A")));
        EXPECT_TRUE(_selection.Toggle(Node("B")));
        ExpectSelection({"A", "B"});

        EXPECT_TRUE(_selection.Toggle(Node("B")));
        ExpectSelection({"A"});
        EXPECT_EQ(_selection.Primary(), Node("A"));
        EXPECT_FALSE(_selection.Toggle(nullptr));
    }

    TEST_F(SceneSelectionTest, TogglingThePrimaryOffMakesThePreviousOneThePrimary) {
        _selection.Add(Node("A"));
        _selection.Add(Node("B"));
        _selection.Add(Node("C"));

        _selection.Toggle(Node("C"));

        EXPECT_EQ(_selection.Primary(), Node("B"));
        EXPECT_FALSE(_selection.Contains(Node("C")));
    }

    TEST_F(SceneSelectionTest, ClearReportsWhetherAnythingWasSelected) {
        EXPECT_FALSE(_selection.Clear());
        _selection.Add(Node("A"));
        EXPECT_TRUE(_selection.Clear());
        EXPECT_EQ(_selection.Primary(), nullptr);
    }

    TEST_F(SceneSelectionTest, ANoModifierClickReplacesTheSelection) {
        _selection.Add(Node("A"));
        _selection.Add(Node("B"));

        EXPECT_TRUE(ApplyClick(_selection, Node("C"), 0));

        ExpectSelection({"C"});
    }

    TEST_F(SceneSelectionTest, ACtrlClickTogglesAndAShiftClickAdds) {
        _selection.Set(Node("A"));

        EXPECT_TRUE(ApplyClick(_selection, Node("B"), kSelectionModifierCtrl));
        ExpectSelection({"A", "B"});
        EXPECT_TRUE(ApplyClick(_selection, Node("B"), kSelectionModifierCtrl));
        ExpectSelection({"A"});

        EXPECT_TRUE(ApplyClick(_selection, Node("C"), kSelectionModifierShift));
        ExpectSelection({"A", "C"});
        EXPECT_FALSE(ApplyClick(_selection, Node("C"), kSelectionModifierShift));
        ExpectSelection({"A", "C"});
    }

    TEST_F(SceneSelectionTest, CtrlWinsWhenBothModifiersAreHeld) {
        _selection.Set(Node("A"));

        ApplyClick(_selection, Node("A"), kSelectionModifierCtrl | kSelectionModifierShift);

        EXPECT_EQ(_selection.Count(), 0u);
    }

    TEST_F(SceneSelectionTest, AClickOnNothingClearsOnlyWithoutAModifier) {
        _selection.Add(Node("A"));
        _selection.Add(Node("B"));

        EXPECT_FALSE(ApplyClick(_selection, nullptr, kSelectionModifierCtrl));
        EXPECT_FALSE(ApplyClick(_selection, nullptr, kSelectionModifierShift));
        EXPECT_EQ(_selection.Count(), 2u);

        EXPECT_TRUE(ApplyClick(_selection, nullptr, 0));
        EXPECT_EQ(_selection.Count(), 0u);
        EXPECT_FALSE(ApplyClick(_selection, nullptr, 0));
    }

    TEST_F(SceneSelectionTest, PruneDropsADeletedNodeAndItsSubtreeAndKeepsTheRest) {
        _selection.Add(Node("A"));
        _selection.Add(Node("B"));
        _selection.Add(Node("B1"));
        _selection.Add(Node("C"));
        ASSERT_TRUE(HierarchyModel::ApplyOperation(*_scene, {HierarchyModel::OperationKind::Delete, Node("B"), nullptr, 0}).changed);

        EXPECT_TRUE(_selection.Prune(*_scene));

        ExpectSelection({"A", "C"});
        EXPECT_EQ(_selection.Primary(), Node("C"));
        EXPECT_FALSE(_selection.Prune(*_scene));
    }

    TEST_F(SceneSelectionTest, PruneDropsANodeOfAnotherSceneAndTheRoot) {
        SceneNode* elsewhere = Find(_manager->GetSceneByName("Other"_intern)->GetRootNode(), "Elsewhere");
        ASSERT_NE(elsewhere, nullptr);
        _selection.Add(elsewhere);
        _selection.Add(Node("A"));

        EXPECT_TRUE(_selection.Prune(*_scene));

        ExpectSelection({"A"});
    }

    TEST_F(SceneSelectionTest, ClearKeepsTheStorageSoASteadyFrameDoesNotAllocate) {
        for (const char* name : {"A", "B", "B1", "C"}) {
            _selection.Add(Node(name));
        }
        SceneNode* const* data = _selection.Nodes().data();

        _selection.Clear();
        for (const char* name : {"C", "B1", "B", "A"}) {
            _selection.Add(Node(name));
        }

        EXPECT_EQ(_selection.Nodes().data(), data);
    }

}  // namespace BECore::Tests
