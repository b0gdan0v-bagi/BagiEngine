#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Widgets/Viewport/ViewportUIGizmos.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;

        // "Main" has a canvas of 800 x 600 units that is the output (scale 1); "Other" is a second scene to tell apart from the active one.
        constexpr const char* kScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="3"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Other"><nodes>
    <node name="Elsewhere"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
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

        void SetRect(UIElement& element, UILength x, UILength y, UILength width, UILength height) {
            element.style.rect.x = x;
            element.style.rect.y = y;
            element.style.rect.width = width;
            element.style.rect.height = height;
        }

        void ExpectRect(const Rect& actual, const Rect& expected) {
            EXPECT_NEAR(actual.x, expected.x, kTolerance);
            EXPECT_NEAR(actual.y, expected.y, kTolerance);
            EXPECT_NEAR(actual.w, expected.w, kTolerance);
            EXPECT_NEAR(actual.h, expected.h, kTolerance);
        }

        /// What UIRectDragEvent told, with the specs copied
        class DragRecorder : public SubscriptionHolder {
        public:
            struct Entry {
                UIRectDragPhase phase;
                const Scene* scene;
                const SceneNode* node;
                const UIElement* element;
                UIRectSpec before;
                UIRectSpec after;
                bool exact;
            };

            DragRecorder() {
                Subscribe<UIRectDragEvent, &DragRecorder::OnDrag>(this);
            }

            Vector<Entry> entries;

        private:
            void OnDrag(const UIRectDragEvent& event) {
                entries.push_back({event.phase, event.scene, event.node, event.element, event.before, event.after, event.exact});
            }
        };

    }  // namespace

    class ViewportUIGizmosTest : public ::testing::Test {
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

            _hud = Find(GetScene("Main").GetRootNode(), "Hud");
            _canvas = _hud->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);
            // A panel with a child inside it, a percent element, a limited one and a bottom right one
            _panel = _canvas->GetRoot().AddChild("Panel"_intern);
            SetRect(*_panel, UILength::Px(100.0f), UILength::Px(50.0f), UILength::Px(400.0f), UILength::Px(300.0f));
            _child = _panel->AddChild("Child"_intern);
            SetRect(*_child, UILength::Px(20.0f), UILength::Px(10.0f), UILength::Px(100.0f), UILength::Px(60.0f));
            _percent = _canvas->GetRoot().AddChild("Percent"_intern);
            SetRect(*_percent, UILength::Percent(10.0f), UILength::Px(400.0f), UILength::Percent(25.0f), UILength::Px(40.0f));
            _limited = _canvas->GetRoot().AddChild("Limited"_intern);
            SetRect(*_limited, UILength::Px(600.0f), UILength::Px(400.0f), UILength::Px(100.0f), UILength::Px(50.0f));
            _limited->style.rect.maxWidth = UILength::Px(150.0f);
            _canvas->Invalidate();
            Draw();
        }

        void TearDown() override {
            _recorder.entries.clear();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        void Draw() {
            _manager->SetActiveScene("Main"_intern);
            _manager->DrawAll();
        }

        Scene& GetScene(const char* name) const {
            return *_manager->GetSceneByName(PoolString::Intern(name));
        }

        Scene& Main() const {
            return GetScene("Main");
        }

        bool BeginMove(UIElement& element, UIDragHandle handle, Vec2 pointer) {
            return _session.BeginHandle(Main(), *_hud, *_canvas, element, handle, pointer);
        }

        static UIElementDragInput At(float x, float y) {
            UIElementDragInput input;
            input.pointer = {x, y};
            input.snap = false;
            return input;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        SceneNode* _hud = nullptr;
        IntrusivePtr<UICanvasComponent> _canvas;
        IntrusivePtr<UIElement> _panel;
        IntrusivePtr<UIElement> _child;
        IntrusivePtr<UIElement> _percent;
        IntrusivePtr<UIElement> _limited;
        UIElementEditSession _session;
        DragRecorder _recorder;
    };

    TEST_F(ViewportUIGizmosTest, AMoveWritesTheRectAndTellsBeginChangeAndCommit) {
        const UIRectSpec start = _child->style.rect;
        ASSERT_TRUE(BeginMove(*_child, UIDragHandle::Body, {150.0f, 80.0f}));
        _session.Update(At(180.0f, 95.0f));
        _session.Finish();

        EXPECT_FALSE(_session.IsActive());
        EXPECT_EQ(_child->style.rect.x, UILength::Px(50.0f));
        EXPECT_EQ(_child->style.rect.y, UILength::Px(25.0f));
        ASSERT_EQ(_recorder.entries.size(), 3u);
        EXPECT_EQ(_recorder.entries[0].phase, UIRectDragPhase::Begin);
        EXPECT_EQ(_recorder.entries[0].before, start);
        EXPECT_EQ(_recorder.entries[0].after, start);
        EXPECT_EQ(_recorder.entries[1].phase, UIRectDragPhase::Change);
        EXPECT_EQ(_recorder.entries[1].before, start);
        EXPECT_EQ(_recorder.entries[1].after, _child->style.rect);
        EXPECT_EQ(_recorder.entries[2].phase, UIRectDragPhase::Commit);
        EXPECT_EQ(_recorder.entries[2].before, start);
        EXPECT_EQ(_recorder.entries[2].after, _child->style.rect);
        for (const auto& entry : _recorder.entries) {
            EXPECT_EQ(entry.scene, &Main());
            EXPECT_EQ(entry.node, _hud);
            EXPECT_EQ(entry.element, _child.Get());
            EXPECT_TRUE(entry.exact);
        }

        Draw();
        ExpectRect(_child->rect, {150.0f, 75.0f, 100.0f, 60.0f});
    }

    TEST_F(ViewportUIGizmosTest, ADragThatComesBackToTheStartGivesTheStartAndCancels) {
        const UIRectSpec start = _child->style.rect;
        ASSERT_TRUE(BeginMove(*_child, UIDragHandle::Body, {150.0f, 80.0f}));
        _session.Update(At(200.0f, 80.0f));
        _session.Update(At(150.0f, 80.0f));
        _session.Finish();

        EXPECT_EQ(_child->style.rect, start);
        ASSERT_EQ(_recorder.entries.size(), 4u);
        EXPECT_EQ(_recorder.entries[3].phase, UIRectDragPhase::Cancel);
    }

    TEST_F(ViewportUIGizmosTest, AReleaseWithoutMovementCancels) {
        ASSERT_TRUE(BeginMove(*_child, UIDragHandle::Body, {150.0f, 80.0f}));
        _session.Finish();

        ASSERT_EQ(_recorder.entries.size(), 2u);
        EXPECT_EQ(_recorder.entries[0].phase, UIRectDragPhase::Begin);
        EXPECT_EQ(_recorder.entries[1].phase, UIRectDragPhase::Cancel);
        EXPECT_FALSE(_session.IsActive());
    }

    TEST_F(ViewportUIGizmosTest, TwoFramesWithTheSamePointerTellOneChange) {
        ASSERT_TRUE(BeginMove(*_child, UIDragHandle::Body, {150.0f, 80.0f}));
        _session.Update(At(170.0f, 80.0f));
        _session.Update(At(170.0f, 80.0f));
        _session.Update(At(171.0f, 80.0f));

        size_t changes = 0;
        for (const auto& entry : _recorder.entries) {
            changes += entry.phase == UIRectDragPhase::Change ? 1 : 0;
        }
        EXPECT_EQ(changes, 2u);
        EXPECT_EQ(_recorder.entries.back().before.x, UILength::Px(40.0f));
        EXPECT_EQ(_recorder.entries.back().after.x, UILength::Px(41.0f));
        _session.Abort(&Main());
    }

    TEST_F(ViewportUIGizmosTest, AbortGivesTheStartBackAndTellsCancelWithTheLastShown) {
        const UIRectSpec start = _child->style.rect;
        ASSERT_TRUE(BeginMove(*_child, UIDragHandle::Body, {150.0f, 80.0f}));
        _session.Update(At(190.0f, 80.0f));
        const UIRectSpec shown = _child->style.rect;
        _session.Abort(&Main());

        EXPECT_EQ(_child->style.rect, start);
        EXPECT_FALSE(_session.IsActive());
        const auto& cancel = _recorder.entries.back();
        EXPECT_EQ(cancel.phase, UIRectDragPhase::Cancel);
        EXPECT_EQ(cancel.before, shown);
        EXPECT_EQ(cancel.after, start);
        EXPECT_EQ(cancel.node, _hud);

        const size_t told = _recorder.entries.size();
        _session.Abort(&Main());
        _session.Finish();
        _session.Update(At(300.0f, 80.0f));
        EXPECT_EQ(_recorder.entries.size(), told);
    }

    TEST_F(ViewportUIGizmosTest, ADragEndsWhenTheSceneIsNotTheActiveOneOrTheModeLeftEdit) {
        ASSERT_TRUE(BeginMove(*_child, UIDragHandle::Body, {150.0f, 80.0f}));

        EXPECT_TRUE(_session.CanContinue(&Main(), true));
        EXPECT_FALSE(_session.CanContinue(&GetScene("Other"), true));
        EXPECT_FALSE(_session.CanContinue(nullptr, true));
        EXPECT_FALSE(_session.CanContinue(&Main(), false));
        _session.Abort(&Main());
        EXPECT_FALSE(_session.CanContinue(&Main(), true));
    }

    TEST_F(ViewportUIGizmosTest, ADragEndsWhenTheElementLeftTheTree) {
        ASSERT_TRUE(BeginMove(*_child, UIDragHandle::Body, {150.0f, 80.0f}));
        _session.Update(At(170.0f, 80.0f));

        _panel->children.clear();

        EXPECT_FALSE(_session.CanContinue(&Main(), true));
        const UIRectSpec shown = _child->style.rect;
        _session.Abort(&Main());
        EXPECT_NE(_child->style.rect, shown);  // the element is held, so the start still goes back to it
        EXPECT_EQ(_recorder.entries.back().phase, UIRectDragPhase::Cancel);
    }

    TEST_F(ViewportUIGizmosTest, ADragEndsWhenTheNodeIsLockedAndAbortsWithoutTheNodeWhenItIsGone) {
        ASSERT_TRUE(BeginMove(*_child, UIDragHandle::Body, {150.0f, 80.0f}));
        _hud->SetEditorFlags(EditorFlags::Locked);
        EXPECT_FALSE(_session.CanContinue(&Main(), true));
        _hud->SetEditorFlags(EditorFlags::None);
        EXPECT_TRUE(_session.CanContinue(&Main(), true));

        _session.Update(At(170.0f, 80.0f));
        const UIRectSpec start = _recorder.entries.front().before;
        Main().GetRootNode().RemoveChild(_hud);
        _hud = nullptr;
        _canvas = nullptr;

        EXPECT_FALSE(_session.CanContinue(&Main(), true));
        _session.Abort(&Main());
        EXPECT_EQ(_child->style.rect, start);
        EXPECT_EQ(_recorder.entries.back().phase, UIRectDragPhase::Cancel);
        EXPECT_EQ(_recorder.entries.back().node, nullptr);
        EXPECT_EQ(_recorder.entries.back().element, _child.Get());
    }

    TEST_F(ViewportUIGizmosTest, AbortInAnotherSceneNamesNoNode) {
        ASSERT_TRUE(BeginMove(*_child, UIDragHandle::Body, {150.0f, 80.0f}));
        _session.Update(At(170.0f, 80.0f));
        _session.Abort(&GetScene("Other"));

        EXPECT_EQ(_recorder.entries.back().node, nullptr);
        EXPECT_EQ(_child->style.rect.x, UILength::Px(20.0f));
    }

    TEST_F(ViewportUIGizmosTest, ADragDoesNotBeginForALockedNodeNoHandleOrAnElementUnderAVariantOverride) {
        EXPECT_FALSE(BeginMove(*_child, UIDragHandle::None, {150.0f, 80.0f}));

        _hud->SetEditorFlags(EditorFlags::Locked);
        EXPECT_FALSE(BeginMove(*_child, UIDragHandle::Body, {150.0f, 80.0f}));
        _hud->SetEditorFlags(EditorFlags::None);

        _child->active.rect.x = UILength::Px(77.0f);  // what a variant that overrides the rect leaves
        EXPECT_FALSE(BeginMove(*_child, UIDragHandle::Body, {150.0f, 80.0f}));
        EXPECT_TRUE(_recorder.entries.empty());
    }

    TEST_F(ViewportUIGizmosTest, ABeginDuringADragIsRefused) {
        ASSERT_TRUE(BeginMove(*_child, UIDragHandle::Body, {150.0f, 80.0f}));
        EXPECT_FALSE(BeginMove(*_panel, UIDragHandle::Body, {300.0f, 200.0f}));
        EXPECT_EQ(_session.GetElement(), _child.Get());
        _session.Abort(&Main());
    }

    TEST_F(ViewportUIGizmosTest, AResizeOfAPercentElementStaysPercent) {
        // Percent is at x = 80 (10 percent of 800), 200 wide (25 percent): the right edge is at 280
        ASSERT_TRUE(BeginMove(*_percent, UIDragHandle::Right, {280.0f, 420.0f}));
        _session.Update(At(320.0f, 420.0f));
        _session.Finish();

        EXPECT_EQ(_percent->style.rect.width.unit, UIUnit::Percent);
        EXPECT_NEAR(_percent->style.rect.width.value, 30.0f, kTolerance);
        EXPECT_EQ(_percent->style.rect.x, UILength::Percent(10.0f));
        EXPECT_EQ(_percent->style.rect.height, UILength::Px(40.0f));
        Draw();
        ExpectRect(_percent->rect, {80.0f, 400.0f, 240.0f, 40.0f});
    }

    TEST_F(ViewportUIGizmosTest, ALimitThatKeepsTheSizeIsCarriedAsNotExact) {
        // Limited is 100 wide with a maximum of 150; a drag to 200 is shown as 150
        ASSERT_TRUE(BeginMove(*_limited, UIDragHandle::Right, {700.0f, 425.0f}));
        _session.Update(At(800.0f, 425.0f));

        EXPECT_FALSE(_session.IsExact());
        EXPECT_FALSE(_recorder.entries.back().exact);
        _session.Finish();
        EXPECT_EQ(_recorder.entries.back().phase, UIRectDragPhase::Commit);
        EXPECT_FALSE(_recorder.entries.back().exact);
    }

    TEST_F(ViewportUIGizmosTest, TheSnapMovesTheEdgeOntoTheLineOfTheBoxAndNamesIt) {
        // Child is in the panel (100..500 by 50..350): its left edge at 120 snaps to the panel's left line at 100
        ASSERT_TRUE(BeginMove(*_child, UIDragHandle::Left, {120.0f, 80.0f}));
        UIElementDragInput input = At(106.0f, 80.0f);
        input.snap = true;
        input.snapThreshold = 8.0f;
        _session.Update(input);

        ASSERT_TRUE(_session.Guides().guideX.has_value());
        EXPECT_NEAR(*_session.Guides().guideX, 100.0f, kTolerance);
        EXPECT_EQ(_child->style.rect.x, UILength::Px(0.0f));
        EXPECT_EQ(_child->style.rect.width, UILength::Px(120.0f));

        input.snap = false;
        _session.Update(input);
        EXPECT_FALSE(_session.Guides().guideX.has_value());
        _session.Abort(&Main());
    }

    TEST_F(ViewportUIGizmosTest, AnAnchorDragKeepsTheRectWhereItWas) {
        Draw();
        const Rect before = _child->rect;
        // The anchors box is the panel: 400 x 300 at (100, 50); the pointer goes to the middle of it
        ASSERT_TRUE(_session.BeginAnchor(Main(), *_hud, *_canvas, *_child, AnchorCorner::TopLeft));
        _session.Update(At(300.0f, 200.0f));
        _session.Finish();

        EXPECT_EQ(_recorder.entries.back().phase, UIRectDragPhase::Commit);
        ASSERT_TRUE(_child->style.rect.anchorMin.has_value() || _child->style.rect.anchor == UIAnchor::Center);
        Draw();
        ExpectRect(_child->rect, before);
    }

    TEST_F(ViewportUIGizmosTest, AnAnchorDragOfAnElementWithoutAnchorsDoesNotBegin) {
        _child->style.visible = false;
        _canvas->Invalidate();
        Draw();
        EXPECT_FALSE(_session.BeginAnchor(Main(), *_hud, *_canvas, *_child, AnchorCorner::TopLeft));
        EXPECT_TRUE(_recorder.entries.empty());
    }

}  // namespace BECore::Tests
