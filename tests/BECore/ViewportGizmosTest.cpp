#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/Viewport/ViewportGizmos.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;

        // "Main": A and B are movable; Locked is movable once the editor locks it; Hud has a canvas and no transform; Plain has a transform and nothing else.
        // "Fit" shows a 400 x 200 view in the 800 x 600 output (scale 2, a bar of 100 pixels above and below), so a scene rect is not its output rect.
        constexpr const char* kScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="A"><components>
      <component type="TransformComponent" x="10" y="20" width="100" height="50" rotation="30" pivotX="0.25" pivotY="0.75"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="B"><components>
      <component type="TransformComponent" x="200" y="40" width="60" height="60"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="Locked"><components>
      <component type="TransformComponent" x="300" y="40" width="60" height="60"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="Hud" layer="3"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Other"><nodes>
    <node name="Elsewhere"><components>
      <component type="TransformComponent" x="1" y="2" width="3" height="4"/>
      <component type="QuadRendererComponent"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Fit" viewWidth="400" viewHeight="200"><nodes>
    <node name="Box"><components>
      <component type="TransformComponent" x="50" y="50" width="100" height="100"/>
      <component type="QuadRendererComponent"/>
    </components></node>
    <node name="Box2"><components>
      <component type="TransformComponent" x="200" y="60" width="50" height="40"/>
      <component type="QuadRendererComponent"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        SceneNode* Find(SceneNode& from, const char* name) {
            const PoolString wanted = PoolString::Intern(name);
            for (const auto& child : from.GetChildren()) {
                if (child->GetName() == wanted) {
                    return child.Get();
                }
            }
            return nullptr;
        }

        Gizmo::Item Moved(Gizmo::Item item, float dx) {
            item.x += dx;
            return item;
        }

        /// What NodeTransformEditEvent told, with the edits copied (the event's own are valid for the call only)
        class EditRecorder : public SubscriptionHolder {
        public:
            struct Entry {
                NodeTransformEditPhase phase;
                const Scene* scene;
                eastl::vector<NodeTransformEdit> edits;
            };

            EditRecorder() {
                Subscribe<NodeTransformEditEvent, &EditRecorder::OnEdit>(this);
            }

            eastl::vector<Entry> entries;

        private:
            void OnEdit(const NodeTransformEditEvent& event) {
                entries.push_back({event.phase, event.scene, eastl::vector<NodeTransformEdit>(event.edits.begin(), event.edits.end())});
            }
        };

    }  // namespace

    class ViewportGizmosTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _manager->SetActiveScene("Fit"_intern);
            _manager->DrawAll();
            _manager->SetActiveScene("Main"_intern);
            _manager->DrawAll();
            Node("Main", "Locked").SetEditorFlags(EditorFlags::Locked);
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        Scene& GetScene(const char* name) const {
            return *_manager->GetSceneByName(PoolString::Intern(name));
        }

        SceneNode& Node(const char* sceneName, const char* name) const {
            SceneNode* node = Find(GetScene(sceneName).GetRootNode(), name);
            EXPECT_NE(node, nullptr) << name;
            return *node;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        TransformComponent& TransformOf(const char* name) const {
            return *Node("Main", name).GetComponent<TransformComponent>();
        }

        /// A free move of A and B (World axes, no snap) that starts at the origin, so the point of an Update is the offset
        bool BeginMove(GizmoEditSession& session, std::initializer_list<const char*> names, const Gizmo::SnapSettings& snap = {}) {
            _nodes.clear();
            _items.clear();
            for (const char* name : names) {
                _nodes.push_back(&Node("Main", name));
                _items.push_back(ItemOf(TransformOf(name)));
            }
            return session.Begin(GetScene("Main"), {_nodes.data(), _nodes.size()}, {_items.data(), _items.size()}, Gizmo::Tool::Translate, Gizmo::Space::World, Gizmo::Handle::Free, {0.0f, 0.0f},
                                 snap);
        }

        eastl::vector<SceneNode*> _nodes;
        eastl::vector<Gizmo::Item> _items;
        EditRecorder _recorder;
    };

    // ---- the frame ------------------------------------------------------------------------------------------------------

    TEST(ViewportFrameTest, AnIdentityCameraShowsTheTargetExactlyWhereTheFitPutsIt) {
        const ViewportFrame frame = MakeViewportFrame(ViewportCamera{}, {100.0f, 50.0f}, {400.0f, 300.0f}, {800.0f, 600.0f}, 0.5f);

        EXPECT_NEAR(frame.origin.x, 100.0f, kTolerance);
        EXPECT_NEAR(frame.origin.y, 50.0f, kTolerance);
        EXPECT_NEAR(frame.end.x, 500.0f, kTolerance);
        EXPECT_NEAR(frame.end.y, 350.0f, kTolerance);
        EXPECT_NEAR(frame.scale, 0.5f, kTolerance);
        EXPECT_NEAR(frame.imageMax.x, 500.0f, kTolerance);
        EXPECT_NEAR(frame.imageMax.y, 350.0f, kTolerance);
    }

    TEST(ViewportFrameTest, ACameraMovesAndScalesTheTargetAndTheFrameRoundTrips) {
        ViewportCamera camera;
        camera.ZoomAt(2.0f, {300.0f, 200.0f}, {100.0f, 50.0f, 400.0f, 300.0f}, 0.5f);
        camera.PanBy({30.0f, -10.0f}, 0.5f);

        const ViewportFrame frame = MakeViewportFrame(camera, {100.0f, 50.0f}, {400.0f, 300.0f}, {800.0f, 600.0f}, 0.5f);

        EXPECT_NEAR(frame.scale, 1.0f, kTolerance);
        EXPECT_NEAR(frame.end.x - frame.origin.x, 800.0f, kTolerance);
        const Vec2 target = frame.ToTarget(frame.ToScreen({123.0f, 456.0f}));
        EXPECT_NEAR(target.x, 123.0f, kTolerance);
        EXPECT_NEAR(target.y, 456.0f, kTolerance);
        // The camera agrees with the frame about where a target pixel is
        const Vec2 byCamera = camera.ToScreen({123.0f, 456.0f}, {100.0f, 50.0f, 400.0f, 300.0f}, 0.5f);
        const ImVec2 byFrame = frame.ToScreen({123.0f, 456.0f});
        EXPECT_NEAR(byFrame.x, byCamera.x, kTolerance);
        EXPECT_NEAR(byFrame.y, byCamera.y, kTolerance);
    }

    // ---- the targets of a gizmo -----------------------------------------------------------------------------------------

    TEST_F(ViewportGizmosTest, AnItemIsTheFieldsOfTheTransformAndBack) {
        const auto transform = Node("Main", "A").GetComponent<TransformComponent>();
        const Gizmo::Item item = ItemOf(*transform);

        EXPECT_EQ(item, (Gizmo::Item{10.0f, 20.0f, 100.0f, 50.0f, 30.0f, 0.25f, 0.75f}));

        ApplyItem(*transform, Gizmo::Item{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0.1f, 0.2f});
        EXPECT_EQ(ItemOf(*transform), (Gizmo::Item{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 0.1f, 0.2f}));
    }

    TEST_F(ViewportGizmosTest, ANodeIsMovableWithATransformAndNotLocked) {
        EXPECT_TRUE(IsMovable(Node("Main", "A")));
        EXPECT_FALSE(IsMovable(Node("Main", "Locked")));
        EXPECT_FALSE(IsMovable(Node("Main", "Hud")));  // a canvas-only node has no gizmo
    }

    TEST_F(ViewportGizmosTest, CollectKeepsTheMovableAliveNodesInTheOrderOfTheSelection) {
        SceneNode* const a = &Node("Main", "A");
        SceneNode* const b = &Node("Main", "B");
        SceneNode* const stranger = &Node("Other", "Elsewhere");  // a node of another tree is not alive in this one
        const eastl::vector<SceneNode*> selected{b, &Node("Main", "Locked"), stranger, &Node("Main", "Hud"), a};

        CollectMovable(GetScene("Main"), {selected.data(), selected.size()}, _nodes, _items);

        ASSERT_EQ(_nodes.size(), 2u);
        EXPECT_EQ(_nodes[0], b);
        EXPECT_EQ(_nodes[1], a);
        ASSERT_EQ(_items.size(), 2u);
        EXPECT_FLOAT_EQ(_items[0].x, 200.0f);
        EXPECT_FLOAT_EQ(_items[1].x, 10.0f);
    }

    TEST_F(ViewportGizmosTest, CollectClearsTheVectorsAndKeepsTheirCapacity) {
        const eastl::vector<SceneNode*> selected{&Node("Main", "A"), &Node("Main", "B")};
        CollectMovable(GetScene("Main"), {selected.data(), selected.size()}, _nodes, _items);
        const size_t nodeCapacity = _nodes.capacity();
        const size_t itemCapacity = _items.capacity();

        CollectMovable(GetScene("Main"), {}, _nodes, _items);

        EXPECT_TRUE(_nodes.empty());
        EXPECT_TRUE(_items.empty());
        EXPECT_EQ(_nodes.capacity(), nodeCapacity);
        EXPECT_EQ(_items.capacity(), itemCapacity);
    }

    TEST_F(ViewportGizmosTest, WriteItemsSetsTheTransformsOfTheNodes) {
        SceneNode* const a = &Node("Main", "A");
        SceneNode* const b = &Node("Main", "B");
        const eastl::vector<SceneNode*> nodes{a, b};
        const eastl::vector<Gizmo::Item> items{Moved(ItemOf(*a->GetComponent<TransformComponent>()), 5.0f), Moved(ItemOf(*b->GetComponent<TransformComponent>()), -7.0f)};

        EXPECT_TRUE(WriteItems(GetScene("Main"), {nodes.data(), nodes.size()}, {items.data(), items.size()}));

        EXPECT_FLOAT_EQ(a->GetComponent<TransformComponent>()->_x, 15.0f);
        EXPECT_FLOAT_EQ(b->GetComponent<TransformComponent>()->_x, 193.0f);
    }

    TEST_F(ViewportGizmosTest, WriteItemsWritesNothingWhenANodeIsGoneOrTheSizesDiffer) {
        SceneNode* const a = &Node("Main", "A");
        SceneNode* const stranger = &Node("Other", "Elsewhere");
        const eastl::vector<SceneNode*> nodes{a, stranger};
        const eastl::vector<Gizmo::Item> items{Moved(ItemOf(*a->GetComponent<TransformComponent>()), 5.0f), Gizmo::Item{}};

        EXPECT_FALSE(AllAlive(GetScene("Main"), {nodes.data(), nodes.size()}));
        EXPECT_FALSE(WriteItems(GetScene("Main"), {nodes.data(), nodes.size()}, {items.data(), items.size()}));
        EXPECT_FLOAT_EQ(a->GetComponent<TransformComponent>()->_x, 10.0f);  // the live node was not touched either

        EXPECT_FALSE(WriteItems(GetScene("Main"), {nodes.data(), 1}, {items.data(), 2}));
        EXPECT_FLOAT_EQ(a->GetComponent<TransformComponent>()->_x, 10.0f);
    }

    // ---- a drag of the gizmo --------------------------------------------------------------------------------------------

    TEST_F(ViewportGizmosTest, ADragTellsBeginChangeAndCommitAndLeavesTheNodesMoved) {
        GizmoEditSession session;
        ASSERT_TRUE(BeginMove(session, {"A", "B"}));
        ASSERT_EQ(_recorder.entries.size(), 1u);
        EXPECT_EQ(_recorder.entries[0].phase, NodeTransformEditPhase::Begin);
        ASSERT_EQ(_recorder.entries[0].edits.size(), 2u);
        EXPECT_EQ(_recorder.entries[0].edits[0].before, _recorder.entries[0].edits[0].after);

        session.Update(GetScene("Main"), {10.0f, 5.0f}, false, false);
        EXPECT_FLOAT_EQ(TransformOf("A")._x, 20.0f);
        EXPECT_FLOAT_EQ(TransformOf("A")._y, 25.0f);
        EXPECT_FLOAT_EQ(TransformOf("B")._x, 210.0f);
        ASSERT_EQ(_recorder.entries.size(), 2u);
        EXPECT_EQ(_recorder.entries[1].phase, NodeTransformEditPhase::Change);

        session.Finish(GetScene("Main"));
        ASSERT_EQ(_recorder.entries.size(), 3u);
        const EditRecorder::Entry& commit = _recorder.entries[2];
        EXPECT_EQ(commit.phase, NodeTransformEditPhase::Commit);
        EXPECT_EQ(commit.scene, &GetScene("Main"));
        ASSERT_EQ(commit.edits.size(), 2u);
        EXPECT_EQ(commit.edits[0].node, &Node("Main", "A"));
        EXPECT_FLOAT_EQ(commit.edits[0].before.x, 10.0f);  // the values of the press against the values of the release
        EXPECT_FLOAT_EQ(commit.edits[0].after.x, 20.0f);
        EXPECT_FLOAT_EQ(commit.edits[1].before.x, 200.0f);
        EXPECT_FLOAT_EQ(commit.edits[1].after.x, 210.0f);
        EXPECT_FLOAT_EQ(TransformOf("A")._x, 20.0f);  // a commit keeps the result
        EXPECT_FALSE(session.IsActive());
    }

    TEST_F(ViewportGizmosTest, ChangeIsToldOnlyWhenTheResultDiffers) {
        GizmoEditSession session;
        ASSERT_TRUE(BeginMove(session, {"A"}));

        session.Update(GetScene("Main"), {0.0f, 0.0f}, false, false);  // the pointer did not move
        session.Update(GetScene("Main"), {4.0f, 0.0f}, false, false);
        session.Update(GetScene("Main"), {4.0f, 0.0f}, false, false);  // the same again

        EXPECT_EQ(_recorder.entries.size(), 2u);  // Begin and one Change
    }

    TEST_F(ViewportGizmosTest, AReleaseWithNoMoveEndsAsACancelAndTheNodesAreUntouched) {
        GizmoEditSession session;
        ASSERT_TRUE(BeginMove(session, {"A"}));
        session.Update(GetScene("Main"), {0.0f, 0.0f}, false, false);

        session.Finish(GetScene("Main"));

        ASSERT_EQ(_recorder.entries.size(), 2u);
        EXPECT_EQ(_recorder.entries[1].phase, NodeTransformEditPhase::Cancel);
        EXPECT_FLOAT_EQ(TransformOf("A")._x, 10.0f);
        EXPECT_FALSE(session.IsActive());
    }

    TEST_F(ViewportGizmosTest, ADragThatComesBackToItsStartCommitsNothing) {
        GizmoEditSession session;
        ASSERT_TRUE(BeginMove(session, {"A"}));
        session.Update(GetScene("Main"), {30.0f, 0.0f}, false, false);
        session.Update(GetScene("Main"), {0.0f, 0.0f}, false, false);

        session.Finish(GetScene("Main"));

        EXPECT_EQ(_recorder.entries.back().phase, NodeTransformEditPhase::Cancel);
        EXPECT_FLOAT_EQ(TransformOf("A")._x, 10.0f);
    }

    TEST_F(ViewportGizmosTest, AbortGivesTheStartValuesBackAndTellsCancelWithTheUndoneValues) {
        GizmoEditSession session;
        ASSERT_TRUE(BeginMove(session, {"A", "B"}));
        session.Update(GetScene("Main"), {40.0f, 0.0f}, false, false);

        session.Abort(&GetScene("Main"));

        EXPECT_FLOAT_EQ(TransformOf("A")._x, 10.0f);
        EXPECT_FLOAT_EQ(TransformOf("B")._x, 200.0f);
        const EditRecorder::Entry& cancel = _recorder.entries.back();
        EXPECT_EQ(cancel.phase, NodeTransformEditPhase::Cancel);
        ASSERT_EQ(cancel.edits.size(), 2u);
        EXPECT_FLOAT_EQ(cancel.edits[0].before.x, 10.0f);
        EXPECT_FLOAT_EQ(cancel.edits[0].after.x, 50.0f);  // the value that was undone
        EXPECT_FALSE(session.IsActive());

        const size_t told = _recorder.entries.size();
        session.Abort(&GetScene("Main"));  // no drag: nothing happens, nothing is told
        EXPECT_EQ(_recorder.entries.size(), told);
    }

    TEST_F(ViewportGizmosTest, AbortSkipsANodeThatLeftTheTree) {
        GizmoEditSession session;
        ASSERT_TRUE(BeginMove(session, {"A", "B"}));
        session.Update(GetScene("Main"), {40.0f, 0.0f}, false, false);
        SceneNode* const gone = &Node("Main", "B");
        EXPECT_TRUE(session.CanContinue(&GetScene("Main")));
        GetScene("Main").GetRootNode().RemoveChild(gone);
        EXPECT_FALSE(session.CanContinue(&GetScene("Main")));

        session.Abort(&GetScene("Main"));

        EXPECT_FLOAT_EQ(TransformOf("A")._x, 10.0f);
        const EditRecorder::Entry& cancel = _recorder.entries.back();
        ASSERT_EQ(cancel.edits.size(), 1u);  // the node that is gone is neither written to nor told
        EXPECT_EQ(cancel.edits[0].node, &Node("Main", "A"));
    }

    TEST_F(ViewportGizmosTest, ANodeThatLostItsTransformEndsTheDragAndNothingFalseIsTold) {
        GizmoEditSession session;
        ASSERT_TRUE(BeginMove(session, {"A", "B"}));
        session.Update(GetScene("Main"), {10.0f, 0.0f}, false, false);
        const size_t told = _recorder.entries.size();
        EXPECT_TRUE(session.CanContinue(&GetScene("Main")));

        Node("Main", "B").RemoveComponent(PoolString::Intern("TransformComponent"));
        EXPECT_FALSE(session.CanContinue(&GetScene("Main")));

        // Were the drag to go on, the write fails as a whole: no Change is told and the recorded result stays the last written one
        session.Update(GetScene("Main"), {30.0f, 0.0f}, false, false);
        EXPECT_EQ(_recorder.entries.size(), told);
        EXPECT_FLOAT_EQ(TransformOf("A")._x, 20.0f);

        session.Abort(&GetScene("Main"));
        EXPECT_FLOAT_EQ(TransformOf("A")._x, 10.0f);
        EXPECT_EQ(_recorder.entries.back().phase, NodeTransformEditPhase::Cancel);
    }

    TEST_F(ViewportGizmosTest, AbortInAnotherSceneWritesAndTellsNothing) {
        GizmoEditSession session;
        ASSERT_TRUE(BeginMove(session, {"A"}));
        session.Update(GetScene("Main"), {40.0f, 0.0f}, false, false);

        EXPECT_FALSE(session.CanContinue(&GetScene("Other")));
        EXPECT_FALSE(session.CanContinue(nullptr));
        session.Abort(&GetScene("Other"));

        EXPECT_FLOAT_EQ(TransformOf("A")._x, 50.0f);  // the scene of the drag was not the active one: it is not touched from here
        EXPECT_TRUE(_recorder.entries.back().edits.empty());
        EXPECT_EQ(_recorder.entries.back().phase, NodeTransformEditPhase::Cancel);
        EXPECT_FALSE(session.IsActive());
    }

    TEST_F(ViewportGizmosTest, TheSnapSwitchAndItsModifierDecideTheRoundingOfAMove) {
        Gizmo::SnapSettings snap;
        snap.position = true;
        snap.positionStep = 8.0f;
        GizmoEditSession session;
        ASSERT_TRUE(BeginMove(session, {"A"}, snap));

        session.Update(GetScene("Main"), {3.0f, 0.0f}, false, false);
        EXPECT_FLOAT_EQ(TransformOf("A")._x, 16.0f);  // 10 + 3 = 13 snaps to the nearest multiple of 8

        session.Update(GetScene("Main"), {3.0f, 0.0f}, true, false);  // Ctrl held: the switch is inverted for this update
        EXPECT_FLOAT_EQ(TransformOf("A")._x, 13.0f);
    }

    TEST_F(ViewportGizmosTest, ADragWithNothingToGrabDoesNotStart) {
        GizmoEditSession session;

        EXPECT_FALSE(BeginMove(session, {}));
        EXPECT_FALSE(session.IsActive());
        EXPECT_TRUE(_recorder.entries.empty());

        _nodes = {&Node("Main", "A")};
        _items = {};
        EXPECT_FALSE(session.Begin(GetScene("Main"), {_nodes.data(), _nodes.size()}, {}, Gizmo::Tool::Translate, Gizmo::Space::World, Gizmo::Handle::Free, {0.0f, 0.0f}, {}));
        EXPECT_FALSE(session.Begin(GetScene("Main"), {}, {}, Gizmo::Tool::Translate, Gizmo::Space::World, Gizmo::Handle::None, {0.0f, 0.0f}, {}));
    }

    // ---- framing the selection ------------------------------------------------------------------------------------------

    TEST_F(ViewportGizmosTest, TheBoundsOfASelectionAreTheUnionOfItsOutlinesInOutputPixels) {
        _manager->SetActiveScene("Fit"_intern);
        _manager->DrawAll();
        const eastl::vector<SceneNode*> selected{&Node("Fit", "Box"), &Node("Fit", "Box2")};
        Rect bounds;

        ASSERT_TRUE(SelectionBounds(GetScene("Fit"), {selected.data(), selected.size()}, bounds));

        // Box: (100, 200)-(300, 400) in the output; Box2: (400, 220)-(500, 300)
        EXPECT_NEAR(bounds.x, 100.0f, kTolerance);
        EXPECT_NEAR(bounds.y, 200.0f, kTolerance);
        EXPECT_NEAR(bounds.w, 400.0f, kTolerance);
        EXPECT_NEAR(bounds.h, 200.0f, kTolerance);
    }

    TEST_F(ViewportGizmosTest, NoBoundsForAnEmptySelectionOrNodesOfAnotherScene) {
        Rect bounds{1.0f, 2.0f, 3.0f, 4.0f};
        const eastl::vector<SceneNode*> stranger{&Node("Other", "Elsewhere")};

        EXPECT_FALSE(SelectionBounds(GetScene("Main"), {}, bounds));
        EXPECT_FALSE(SelectionBounds(GetScene("Main"), {stranger.data(), stranger.size()}, bounds));
        EXPECT_FLOAT_EQ(bounds.x, 1.0f);  // untouched
    }

}  // namespace BECore::Tests
