#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/SceneTestSupport.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/SceneEditor/SceneDialogsModel.h>
#include <Widgets/SceneEditor/SceneSelectionEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kTwoScenes = R"(<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Branch"><children><node name="Leaf"/></children></node>
    <node name="Locked"/>
  </nodes></scene>
  <scene type="Scene" name="Other"><nodes><node name="Elsewhere"/></nodes></scene>
</scenes></SceneManager>)";

        constexpr StringView kTransform = "TransformComponent";
        constexpr StringView kNoSuchType = "NoSuchComponent";
        constexpr StringView kNewName = "Fresh";

        /// What the dialogs told the rest of the editor
        class DialogEvents : public SubscriptionHolder {
        public:
            DialogEvents() {
                Subscribe<SceneNodeAddedEvent, &DialogEvents::OnNodeAdded>(this);
                Subscribe<SceneEditedEvent, &DialogEvents::OnSceneEdited>(this);
            }

            int nodesAdded = 0;
            int scenesEdited = 0;
            SceneNode* lastParent = nullptr;
            const Scene* lastScene = nullptr;

        private:
            void OnNodeAdded(const SceneNodeAddedEvent& event) {
                ++nodesAdded;
                lastParent = event.parent;
                lastScene = event.scene;
            }
            void OnSceneEdited(const SceneEditedEvent&) {
                ++scenesEdited;
            }
        };

    }  // namespace

    /// The Add Node and Add Component dialogs' model over a scene of a manager that is the active one of `CoreManager`, with no ImGui
    class SceneDialogsModelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kTwoScenes));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);
            _manager->SetActiveScene("Main"_intern);
            _scene = &Require(_manager->GetActiveScene());
            _model = New<SceneDialogsModel>();
            _model->Initialize();
        }

        void TearDown() override {
            _model.Reset();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
        }

        SceneDialogsView& View() {
            return _model->GetView();
        }

        SceneNode& Node(StringView name) {
            return Require(FindNodeBelow(_scene->GetRootNode(), name));
        }

        bool Command(const PoolString& id, StringView argument = {}) {
            return _model->RunCommand(id.ToStringView(), argument);
        }

        void RequestAddNode(SceneNode* parent) {
            SceneAddNodeRequestEvent::Emit(_scene, parent);
            _model->Update();
        }

        void RequestAddComponent(SceneNode* node) {
            SceneAddComponentRequestEvent::Emit(_scene, node);
            _model->Update();
        }

        IntrusivePtr<SceneManager> _manager;
        Scene* _scene = nullptr;
        IntrusivePtr<SceneDialogsModel> _model;
        DialogEvents _events;
    };

    TEST_F(SceneDialogsModelTest, TheNameStartsAsChildNodeUnderANodeAndAsNewNodeAtTheRoot) {
        RequestAddNode(&Node("Branch"));
        EXPECT_TRUE(View().addNodeOpen);
        EXPECT_EQ(View().nodeName, "ChildNode");
        EXPECT_TRUE(View().canAdd);

        ASSERT_TRUE(Command(EditorCommandIds::kSceneDialogsCancel));
        EXPECT_FALSE(View().addNodeOpen);

        RequestAddNode(nullptr);
        EXPECT_EQ(View().nodeName, "NewNode");
    }

    TEST_F(SceneDialogsModelTest, AnEmptyNameCannotBeAddedAndOkAddsNothingThen) {
        RequestAddNode(&Node("Branch"));
        View().nodeName.clear();
        _model->Update();
        EXPECT_FALSE(View().canAdd);

        EXPECT_TRUE(Command(EditorCommandIds::kSceneDialogsAddNode));
        EXPECT_TRUE(View().addNodeOpen);
        EXPECT_EQ(_events.nodesAdded, 0);
    }

    TEST_F(SceneDialogsModelTest, OkAddsAnAuthoredChildUnderTheParentAndTellsIt) {
        SceneNode& branch = Node("Branch");
        RequestAddNode(&branch);
        View().nodeName.assign(kNewName.data(), kNewName.size());

        ASSERT_TRUE(Command(EditorCommandIds::kSceneDialogsAddNode));

        SceneNode& added = Node(kNewName);
        EXPECT_EQ(added.GetParent(), &branch);
        EXPECT_FALSE(added.IsGenerated()) << "an authored node, so Save writes it";
        EXPECT_FALSE(View().addNodeOpen);
        EXPECT_EQ(_events.nodesAdded, 1);
        EXPECT_EQ(_events.lastParent, &branch);
        EXPECT_EQ(_events.lastScene, _scene);
    }

    TEST_F(SceneDialogsModelTest, AnAddAtTheRootGoesUnderTheRootNode) {
        RequestAddNode(nullptr);
        ASSERT_TRUE(Command(EditorCommandIds::kSceneDialogsAddNode));

        EXPECT_EQ(Node("NewNode").GetParent(), &_scene->GetRootNode());
        EXPECT_EQ(_events.lastParent, &_scene->GetRootNode());
    }

    TEST_F(SceneDialogsModelTest, ANodeRemovedWhileTheDialogIsOpenClosesItAndOkAddsNothing) {
        SceneNode& branch = Node("Branch");
        RequestAddNode(&branch);
        ASSERT_TRUE(View().addNodeOpen);

        _scene->GetRootNode().RemoveChild(&branch);
        EXPECT_TRUE(Command(EditorCommandIds::kSceneDialogsAddNode));

        EXPECT_FALSE(View().addNodeOpen);
        EXPECT_EQ(_events.nodesAdded, 0);
    }

    TEST_F(SceneDialogsModelTest, ASwitchOfTheActiveSceneClosesTheDialogOnTheNextUpdate) {
        RequestAddComponent(&Node("Branch"));
        ASSERT_TRUE(View().addComponentOpen);

        _manager->SetActiveScene("Other"_intern);
        _model->Update();

        EXPECT_FALSE(View().addComponentOpen);
    }

    TEST_F(SceneDialogsModelTest, ARequestForANodeOfAnotherSceneOrNoNodeOpensNothing) {
        SceneAddNodeRequestEvent::Emit(_manager->GetSceneByName("Other"_intern), nullptr);
        EXPECT_FALSE(View().addNodeOpen);

        RequestAddComponent(nullptr);
        EXPECT_FALSE(View().addComponentOpen);
    }

    TEST_F(SceneDialogsModelTest, ATypeAddsAComponentToTheNodeAndTellsTheSceneIsEdited) {
        SceneNode& leaf = Node("Leaf");
        RequestAddComponent(&leaf);
        ASSERT_TRUE(View().addComponentOpen);

        ASSERT_TRUE(Command(EditorCommandIds::kSceneDialogsAddComponent, kTransform));

        EXPECT_NE(leaf.GetComponent(kTransform), nullptr);
        EXPECT_FALSE(View().addComponentOpen);
        EXPECT_EQ(_events.scenesEdited, 1);
    }

    TEST_F(SceneDialogsModelTest, AnUnknownTypeNameAddsNothingAndKeepsTheDialogOpen) {
        SceneNode& leaf = Node("Leaf");
        RequestAddComponent(&leaf);

        ASSERT_TRUE(Command(EditorCommandIds::kSceneDialogsAddComponent, kNoSuchType));

        EXPECT_TRUE(leaf.GetComponents().empty());
        EXPECT_TRUE(View().addComponentOpen);
        EXPECT_EQ(_events.scenesEdited, 0);
    }

    TEST_F(SceneDialogsModelTest, ALockedNodeIsNotOfferedAComponent) {
        SceneNode& locked = Node("Locked");
        locked.SetEditorFlags(EditorFlags::Locked);

        RequestAddComponent(&locked);

        EXPECT_FALSE(View().addComponentOpen);
    }

    TEST_F(SceneDialogsModelTest, TheListHoldsTheRegisteredComponentsWithTheirClassNames) {
        bool hasTransform = false;
        for (const SceneComponentRow& row : View().components) {
            hasTransform = hasTransform || row.typeName == kTransform;
            EXPECT_NE(row.label.find(row.typeName), String::npos) << "the label is the icon and the class name";
        }
        EXPECT_TRUE(hasTransform);
    }

    TEST_F(SceneDialogsModelTest, ACommandOfAnotherModelIsNotTaken) {
        EXPECT_FALSE(Command(EditorCommandIds::kLayoutsSaveDialogSave));
    }

}  // namespace BECore::Tests
