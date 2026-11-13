#include <BECore/GameManager/CoreManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UITooltip.h>
#include <BECore/UI/UITooltipComponent.h>
#include <BECore/UI/UITransitionComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SubscriptionHolder.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The show / hide transitions of an element (`UITransitionComponent`) and of a canvas (the `transition*` fields of `UICanvasComponent`) on the manual clock: what is drawn at a progress, what the
     * layout does at the start and at the end, and who gets input meanwhile. The rules of the numbers themselves are `UITransitionTest`.
     */

    namespace {

        // Hud (output 800 x 600, scale 1, every colour has alpha 200 so the halves are whole numbers). All the transitions are Linear and take one second.
        //   Plain   (10, 10, 100 x 40)    no component
        //   Fader   (10, 60, 100 x 40)    fade; child FaderButton (0, 0, 60 x 40) with a clicker
        //   Mover   (200, 100, 100 x 80)  no fade, scale from 0.5 about its centre, slides 32 down
        //   Late    (10, 200, 100 x 40)   hidden at load
        //   Pop     (10, 260, 100 x 40)   a component with no duration: the old pop
        //   Open    (300, 10, 100 x 40)   a clicker, input stays on during the transition
        //   Under   (500, 10, 100 x 100)  a clicker; Shield above it blocks input and has the component
        //   Frame   (10, 320, 200 x 60)   opacity 0.5 with the child FrameKid (0, 0, 100 x 60) that has the component
        constexpr const char* kTransitionXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Plain" x="10" y="10" width="100" height="40" color="100,110,120,200"/>
          <element type="UIElement" name="Fader" x="10" y="60" width="100" height="40" color="100,110,120,200">
            <components><component type="UITransitionComponent" duration="1" ease="Linear"/></components>
            <children>
              <element type="UIElement" name="FaderButton" x="0" y="0" width="60" height="40" color="10,20,30,200">
                <components><component type="UIInputClicker" action="TransitionFader"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Mover" x="200" y="100" width="100" height="80" color="100,110,120,200">
            <components><component type="UITransitionComponent" duration="1" ease="Linear" fade="false" scaleFrom="0.5" slideY="32"/></components>
          </element>
          <element type="UIElement" name="Late" x="10" y="200" width="100" height="40" color="100,110,120,200" visible="false">
            <components><component type="UITransitionComponent" duration="1" ease="Linear"/></components>
          </element>
          <element type="UIElement" name="Pop" x="10" y="260" width="100" height="40" color="100,110,120,200">
            <components><component type="UITransitionComponent" duration="0"/></components>
          </element>
          <element type="UIElement" name="Open" x="300" y="10" width="100" height="40" color="100,110,120,200">
            <components>
              <component type="UIInputClicker" action="TransitionOpen"/>
              <component type="UITransitionComponent" duration="1" ease="Linear" inputDuringTransition="true"/>
            </components>
          </element>
          <element type="UIElement" name="Under" x="500" y="10" width="100" height="100" color="40,50,60,200">
            <components><component type="UIInputClicker" action="TransitionUnder"/></components>
          </element>
          <element type="UIElement" name="Shield" x="500" y="10" width="100" height="100" color="100,110,120,200" blocksInput="true">
            <components><component type="UITransitionComponent" duration="1" ease="Linear"/></components>
          </element>
          <element type="UIElement" name="Frame" x="10" y="320" width="200" height="60" color="100,110,120,200" opacity="0.5">
            <children>
              <element type="UIElement" name="FrameKid" x="0" y="0" width="100" height="60" color="10,20,30,200">
                <components><component type="UITransitionComponent" duration="1" ease="Linear"/></components>
              </element>
            </children>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        // Two canvases (the same output): Below (layer 5) has a button, Dialog (layer 10) is modal and shows and hides in one second, Linear.
        //   Box (100, 100, 200 x 100) with the button DialogButton (0, 0, 60 x 40), and BelowButton (100, 400, 100 x 40)
        constexpr const char* kCanvasXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Below" layer="5"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="BelowButton" x="100" y="400" width="100" height="40" color="40,50,60,200">
            <components><component type="UIInputClicker" action="CanvasBelow"/></components>
          </element>
        </elements>
      </component>
    </components></node>
    <node name="Dialog" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize" modal="true" transitionDuration="1" transitionEase="Linear">
        <elements>
          <element type="UIElement" name="Box" x="100" y="100" width="200" height="100" color="100,110,120,200">
            <children>
              <element type="UIElement" name="DialogButton" x="0" y="0" width="60" height="40" color="10,20,30,200">
                <components><component type="UIInputClicker" action="CanvasDialog"/></components>
              </element>
            </children>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr double kStep = 0.125;  // an exact binary fraction; a frame is clamped to 0.25 s anyway
        constexpr uint8_t kAuthoredAlpha = 200;
        constexpr float kRectTolerance = 1e-3f;
        constexpr int kSteadyFrames = 8;

        class ActionCounter : public SubscriptionHolder {
        public:
            ActionCounter() {
                Subscribe<InputEvents::ActionEvent, &ActionCounter::OnAction>(this);
            }
            int fader = 0;
            int open = 0;
            int under = 0;
            int dialog = 0;
            int below = 0;

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                fader += event.action == PoolString::Intern("TransitionFader") ? 1 : 0;
                open += event.action == PoolString::Intern("TransitionOpen") ? 1 : 0;
                under += event.action == PoolString::Intern("TransitionUnder") ? 1 : 0;
                dialog += event.action == PoolString::Intern("CanvasDialog") ? 1 : 0;
                below += event.action == PoolString::Intern("CanvasBelow") ? 1 : 0;
            }
        };

        SDL_MouseMotionEvent Motion(float x, float y) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.x = x;
            event.y = y;
            return event;
        }

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        bool SameRect(const Rect& a, const Rect& b) {
            return std::abs(a.x - b.x) < kRectTolerance && std::abs(a.y - b.y) < kRectTolerance && std::abs(a.w - b.w) < kRectTolerance && std::abs(a.h - b.h) < kRectTolerance;
        }

    }  // namespace

    class UITransitionComponentTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();
            CoreManager::GetFontAtlasLibrary().Clear();
            CoreManager::GetTime().Reset();
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kTransitionXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _manager->SetActiveScene(PoolString::Intern("Main"));
            _hud = _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren()[0]->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_hud);
            Draw();
        }

        void TearDown() override {
            _hud.Reset();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
            CoreManager::GetTime().Reset();
        }

        void Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        /// Lets @p seconds of game time pass in frames of `kStep`; the calls of the last frame stay
        void Wait(double seconds) {
            for (double passed = 0.0; passed < seconds - 1e-9; passed += kStep) {
                CoreManager::GetTime().Advance(kStep);
                Draw();
            }
        }

        UIElement& Get(const char* name) const {
            UIElement* element = _hud->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            return *element;
        }

        void Hide(const char* name) {
            ASSERT_TRUE(_hud->SetElementVisible(PoolString::Intern(name), false));
        }
        void Show(const char* name) {
            ASSERT_TRUE(_hud->SetElementVisible(PoolString::Intern(name), true));
        }

        /// The Rect call of the last frame that sits at @p rect (output pixels = canvas units here), null when there is none
        const DrawCall* CallAt(const Rect& rect) const {
            const DrawCall* found = nullptr;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && SameRect(call.rect, rect)) {
                    EXPECT_EQ(found, nullptr) << "drawn once";
                    found = &call;
                }
            }
            return found;
        }

        /// The alpha the element was drawn with at its laid-out rect; nothing when it was not drawn there
        std::optional<uint8_t> AlphaAt(const Rect& rect) const {
            const DrawCall* call = CallAt(rect);
            return call ? std::optional<uint8_t>(call->color.a) : std::nullopt;
        }

        void Click(float x, float y) {
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(x, y));
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));
            CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _hud;
    };

    TEST_F(UITransitionComponentTest, TheFirstLayoutDoesNotAnimateAndAnElementHiddenAtLoadStaysHidden) {
        EXPECT_EQ(AlphaAt(Get("Fader").rect), kAuthoredAlpha);
        EXPECT_FALSE(Get("Fader").GetComponent<UITransitionComponent>()->run.IsTransitioning(CoreManager::GetTime().GetTotalTime()));
        EXPECT_TRUE(Get("Late").rect.IsEmpty());
        const uint32_t runs = _hud->GetLayout().GetLayoutRuns();
        Wait(2.0);
        EXPECT_TRUE(Get("Late").rect.IsEmpty());
        EXPECT_EQ(_hud->GetLayout().GetLayoutRuns(), runs) << "an idle component lays nothing out again";
    }

    TEST_F(UITransitionComponentTest, AHiddenElementKeepsItsRectAndFadesOutThenGoesWithOneLayout) {
        const Rect rect = Get("Fader").rect;
        const uint32_t runs = _hud->GetLayout().GetLayoutRuns();
        Hide("Fader");
        Draw();
        EXPECT_EQ(AlphaAt(rect), kAuthoredAlpha) << "the first frame of a hide is still the whole element";
        EXPECT_EQ(_hud->GetLayout().GetLayoutRuns(), runs + 1) << "the edit laid out once";
        Wait(0.5);
        EXPECT_EQ(AlphaAt(rect), 100);
        EXPECT_TRUE(SameRect(Get("Fader").rect, rect)) << "it keeps its rect while it leaves";
        EXPECT_EQ(AlphaAt(Get("FaderButton").rect), 100) << "the subtree fades with it";
        Wait(0.375);
        EXPECT_EQ(AlphaAt(rect), 25);
        EXPECT_EQ(_hud->GetLayout().GetLayoutRuns(), runs + 1) << "nothing laid out while it leaves";
        Wait(0.125);
        EXPECT_FALSE(AlphaAt(rect).has_value()) << "gone at the end";
        EXPECT_TRUE(Get("Fader").rect.IsEmpty());
        EXPECT_EQ(_hud->GetLayout().GetLayoutRuns(), runs + 2) << "one more layout, for the end";
        Wait(1.0);
        EXPECT_EQ(_hud->GetLayout().GetLayoutRuns(), runs + 2);
    }

    TEST_F(UITransitionComponentTest, AShownElementRisesFromNothingAndArrivesWithOneLayout) {
        Show("Late");
        Draw();
        const Rect rect = Get("Late").rect;
        ASSERT_FALSE(rect.IsEmpty());
        const uint32_t runs = _hud->GetLayout().GetLayoutRuns();
        EXPECT_EQ(AlphaAt(rect), 0);
        Wait(0.5);
        EXPECT_EQ(AlphaAt(rect), 100);
        Wait(0.5);
        EXPECT_EQ(AlphaAt(rect), kAuthoredAlpha);
        EXPECT_EQ(_hud->GetLayout().GetLayoutRuns(), runs + 1) << "one layout, when the run ended";
    }

    TEST_F(UITransitionComponentTest, AReversalInTheMiddleContinuesFromWhatIsShown) {
        const Rect rect = Get("Fader").rect;
        Hide("Fader");
        Draw();
        Wait(0.25);
        EXPECT_EQ(AlphaAt(rect), 150);
        Show("Fader");
        Draw();
        EXPECT_EQ(AlphaAt(rect), 150) << "no jump";
        Wait(0.125);
        EXPECT_EQ(AlphaAt(rect), 175) << "the quarter that is left takes a quarter of the second";
        Wait(0.125);
        EXPECT_EQ(AlphaAt(rect), kAuthoredAlpha);
    }

    TEST_F(UITransitionComponentTest, ANoDurationIsTheOldPop) {
        const Rect rect = Get("Pop").rect;
        ASSERT_TRUE(AlphaAt(rect).has_value());
        Hide("Pop");
        Draw();
        EXPECT_FALSE(AlphaAt(rect).has_value());
        EXPECT_TRUE(Get("Pop").rect.IsEmpty());
        Show("Pop");
        Draw();
        EXPECT_EQ(AlphaAt(rect), kAuthoredAlpha);
    }

    TEST_F(UITransitionComponentTest, TheScaleIsAboutTheCentreAndTheSlideIsInCanvasUnits) {
        const Rect rect = Get("Mover").rect;
        Hide("Mover");
        Draw();
        Wait(0.5);
        // Progress 0.5: scale 0.75 about (250, 140), 16 units of slide left
        const Rect expected{rect.x * 0.75f + 250.0f * 0.25f, rect.y * 0.75f + 140.0f * 0.25f + 16.0f, rect.w * 0.75f, rect.h * 0.75f};
        const DrawCall* call = CallAt(expected);
        ASSERT_NE(call, nullptr);
        EXPECT_EQ(call->color.a, kAuthoredAlpha) << "no fade: only the geometry moves";
        EXPECT_TRUE(SameRect(Get("Mover").rect, rect)) << "the laid-out rect is not scaled";
    }

    TEST_F(UITransitionComponentTest, ATransitioningElementInsideAFadedOneMultiplies) {
        const Rect rect = Get("FrameKid").rect;
        EXPECT_EQ(AlphaAt(rect), 100) << "the parent's 0.5";
        Hide("FrameKid");
        Draw();
        Wait(0.5);
        EXPECT_EQ(AlphaAt(rect), 50);
        EXPECT_EQ(_renderer->GetOpacity(), 1.0f);
        EXPECT_EQ(_renderer->GetViewTransform().scale, 1.0f) << "the view is put back";
    }

    TEST_F(UITransitionComponentTest, ATransitionPutsTheViewAndTheOpacityBackForTheNextSibling) {
        Hide("Mover");
        Draw();
        Wait(0.25);
        EXPECT_EQ(AlphaAt(Get("Late").rect), std::nullopt);
        EXPECT_EQ(AlphaAt(Get("Pop").rect), kAuthoredAlpha) << "a later sibling is not scaled or faded";
        EXPECT_EQ(AlphaAt(Get("Plain").rect), kAuthoredAlpha);
    }

    TEST_F(UITransitionComponentTest, WhileAnInteractiveChildHidesItsClickIsConsumedAndGivesNoAction) {
        ActionCounter actions;
        Hide("Fader");
        Draw();
        Wait(0.25);
        UIElement& button = Get("FaderButton");
        ASSERT_FALSE(button.rect.IsEmpty());
        EXPECT_TRUE(button.inputLocked);
        const float x = button.rect.x + 5.0f;
        const float y = button.rect.y + 5.0f;
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseMotion(Motion(x, y))) << "the canvas still shields what is below";
        EXPECT_FALSE(button.hovered);
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true)));
        EXPECT_FALSE(button.pressed);
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false)));
        EXPECT_EQ(actions.fader, 0);
    }

    TEST_F(UITransitionComponentTest, AClickThatBeganBeforeTheHideAndEndsDuringItIsNoClick) {
        ActionCounter actions;
        UIElement& button = Get("FaderButton");
        const float x = button.rect.x + 5.0f;
        const float y = button.rect.y + 5.0f;
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(x, y));
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));
        EXPECT_TRUE(button.pressed);
        Hide("Fader");
        Draw();
        Wait(0.25);
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
        EXPECT_EQ(actions.fader, 0);
    }

    TEST_F(UITransitionComponentTest, TheLockIsLiftedWhenTheShowEnds) {
        ActionCounter actions;
        Hide("Fader");
        Draw();
        Wait(1.0);
        Show("Fader");
        Draw();
        Wait(0.5);
        UIElement& button = Get("FaderButton");
        ASSERT_FALSE(button.rect.IsEmpty());
        EXPECT_TRUE(button.inputLocked);
        Click(button.rect.x + 5.0f, button.rect.y + 5.0f);
        EXPECT_EQ(actions.fader, 0) << "no click while it comes in";
        Wait(0.5);
        EXPECT_FALSE(button.inputLocked);
        Click(button.rect.x + 5.0f, button.rect.y + 5.0f);
        EXPECT_EQ(actions.fader, 1);
    }

    TEST_F(UITransitionComponentTest, WithInputDuringTransitionTheElementIsAsLiveAsAnyOther) {
        ActionCounter actions;
        const Rect rect = Get("Open").rect;
        Hide("Open");
        Draw();
        Wait(0.25);
        ASSERT_TRUE(AlphaAt(rect).has_value());
        EXPECT_FALSE(Get("Open").inputLocked);
        Click(rect.x + 5.0f, rect.y + 5.0f);
        EXPECT_EQ(actions.open, 1) << "in its laid-out rect, though it is drawn smaller";
    }

    TEST_F(UITransitionComponentTest, ABlockingPanelThatIsLeavingStillShieldsWhatIsBelow) {
        ActionCounter actions;
        const Rect rect = Get("Shield").rect;
        Hide("Shield");
        Draw();
        Wait(0.25);
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(rect.x + 5.0f, rect.y + 5.0f, true)));
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(rect.x + 5.0f, rect.y + 5.0f, false));
        EXPECT_EQ(actions.under, 0);
        Wait(1.0);
        Click(rect.x + 5.0f, rect.y + 5.0f);
        EXPECT_EQ(actions.under, 1) << "when it is gone the panel below is reachable";
    }

    TEST_F(UITransitionComponentTest, ATooltipIsNotShownForALockedElement) {
        const auto plateDrawn = [this] {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && ColorsEqual(call.color, UITooltip::kPlateColor)) {
                    return true;
                }
            }
            return false;
        };
        Get("Open").AddComponent<UITooltipComponent>().text = "Open tip";
        const Rect open = Get("Open").rect;
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(open.x + 5.0f, open.y + 5.0f));
        Wait(UITooltip::kHoverDelay + kStep);
        EXPECT_TRUE(plateDrawn()) << "an element that is live shows its tooltip";

        UIElement& button = Get("FaderButton");
        button.AddComponent<UITooltipComponent>().text = "Fader tip";
        Hide("Fader");
        Draw();
        Wait(0.125);
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(button.rect.x + 5.0f, button.rect.y + 5.0f));
        Wait(UITooltip::kHoverDelay + kStep);
        EXPECT_FALSE(plateDrawn());
    }

    TEST_F(UITransitionComponentTest, AnElementTakenOutOfTheTreeWhileItLeavesDoesNotHangOnToTheRun) {
        Hide("Fader");
        Draw();
        Wait(0.25);
        auto& children = _hud->GetRoot().children;
        const auto fader = eastl::find_if(children.begin(), children.end(), [](const auto& child) { return child->name == PoolString::Intern("Fader"); });
        ASSERT_NE(fader, children.end());
        children.erase(fader);
        _hud->Invalidate();
        Wait(2.0);
        const uint32_t runs = _hud->GetLayout().GetLayoutRuns();
        Wait(1.0);
        EXPECT_EQ(_hud->GetLayout().GetLayoutRuns(), runs) << "nothing of it ticks any more";
    }

    TEST_F(UITransitionComponentTest, ASteadyCanvasWithAnIdleComponentAllocatesNothing) {
        Wait(0.5);
        Wait(0.5);
#if BE_PROFILER_ALLOC
        const uint64_t before = AllocTracker::GetThreadCounters().count;
#endif
        for (int frame = 0; frame < kSteadyFrames; ++frame) {
            CoreManager::GetTime().Advance(kStep);
            Draw();
        }
#if BE_PROFILER_ALLOC
        EXPECT_EQ(AllocTracker::GetThreadCounters().count, before);
#endif
        EXPECT_EQ(AlphaAt(Get("Fader").rect), kAuthoredAlpha);
    }

    // ---- the canvas ----------------------------------------------------------------------------------------------------------

    class UICanvasTransitionTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();
            CoreManager::GetFontAtlasLibrary().Clear();
            CoreManager::GetTime().Reset();
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kCanvasXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _manager->SetActiveScene(PoolString::Intern("Main"));
            _scene = _manager->GetSceneByName(PoolString::Intern("Main"));
            const auto& nodes = _scene->GetRootNode().GetChildren();
            _below = nodes[0]->GetComponent<UICanvasComponent>();
            _dialog = nodes[1]->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_below && _dialog);
            Draw();
        }

        void TearDown() override {
            _below.Reset();
            _dialog.Reset();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
            CoreManager::GetTime().Reset();
        }

        void Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        void Wait(double seconds) {
            for (double passed = 0.0; passed < seconds - 1e-9; passed += kStep) {
                CoreManager::GetTime().Advance(kStep);
                Draw();
            }
        }

        void SetDialogVisible(bool visible) {
            ASSERT_TRUE(UICanvasComponent::SetVisibleByName(*_scene, PoolString::Intern("Dialog"), visible));
        }

        UIElement& Get(UICanvasComponent& canvas, const char* name) const {
            UIElement* element = canvas.GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            return *element;
        }

        const DrawCall* CallAt(const Rect& rect) const {
            const DrawCall* found = nullptr;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && SameRect(call.rect, rect)) {
                    found = &call;
                }
            }
            return found;
        }

        std::optional<uint8_t> AlphaAt(const Rect& rect) const {
            const DrawCall* call = CallAt(rect);
            return call ? std::optional<uint8_t>(call->color.a) : std::nullopt;
        }

        void Click(const UIElement& element) {
            const float x = element.rect.x + 5.0f;
            const float y = element.rect.y + 5.0f;
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(x, y));
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));
            CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        Scene* _scene = nullptr;
        IntrusivePtr<UICanvasComponent> _below;
        IntrusivePtr<UICanvasComponent> _dialog;
    };

    TEST_F(UICanvasTransitionTest, AHiddenCanvasIsDrawnForTheDurationAndThenNoMore) {
        const Rect box = Get(*_dialog, "Box").rect;
        EXPECT_EQ(AlphaAt(box), kAuthoredAlpha);
        SetDialogVisible(false);
        Draw();
        EXPECT_EQ(AlphaAt(box), kAuthoredAlpha) << "the first frame of the hide";
        Wait(0.5);
        EXPECT_EQ(AlphaAt(box), 100);
        EXPECT_EQ(AlphaAt(Get(*_dialog, "DialogButton").rect), 100);
        EXPECT_EQ(_renderer->GetOpacity(), 1.0f);
        Wait(0.375);
        EXPECT_EQ(AlphaAt(box), 25);
        Wait(0.125);
        EXPECT_FALSE(AlphaAt(box).has_value());
        EXPECT_FALSE(AlphaAt(Get(*_dialog, "DialogButton").rect).has_value()) << "nothing of the canvas is drawn after the end";
    }

    TEST_F(UICanvasTransitionTest, AShownCanvasRisesFromNothing) {
        SetDialogVisible(false);
        Draw();
        Wait(1.0);
        const Rect box = Get(*_dialog, "Box").rect;
        EXPECT_FALSE(AlphaAt(box).has_value());
        SetDialogVisible(true);
        Draw();
        EXPECT_EQ(AlphaAt(box), 0);
        Wait(0.5);
        EXPECT_EQ(AlphaAt(box), 100);
        Wait(0.5);
        EXPECT_EQ(AlphaAt(box), kAuthoredAlpha);
    }

    TEST_F(UICanvasTransitionTest, AHideReversedInTheMiddleContinuesFromWhatIsShown) {
        const Rect box = Get(*_dialog, "Box").rect;
        SetDialogVisible(false);
        Draw();
        Wait(0.25);
        EXPECT_EQ(AlphaAt(box), 150);
        SetDialogVisible(true);
        Draw();
        EXPECT_EQ(AlphaAt(box), 150);
        Wait(0.125);
        EXPECT_EQ(AlphaAt(box), 175);
    }

    TEST_F(UICanvasTransitionTest, ANoDurationIsTheOldPop) {
        _dialog->_transitionDuration = 0.0f;
        const Rect box = Get(*_dialog, "Box").rect;
        SetDialogVisible(false);
        Draw();
        EXPECT_FALSE(AlphaAt(box).has_value());
        SetDialogVisible(true);
        Draw();
        EXPECT_EQ(AlphaAt(box), kAuthoredAlpha);
    }

    TEST_F(UICanvasTransitionTest, TheScaleIsAboutTheCentreOfTheCanvasAndTheSlideIsInCanvasUnits) {
        _dialog->_transitionFade = false;
        _dialog->_transitionScaleFrom = 0.5f;
        _dialog->_transitionSlideX = 40.0f;
        _dialog->_transitionSlideY = -20.0f;
        const Rect box = Get(*_dialog, "Box").rect;
        SetDialogVisible(false);
        Draw();
        Wait(0.5);
        // Scale 0.75 about (400, 300); half of the slide is left
        const Rect expected{box.x * 0.75f + 400.0f * 0.25f + 20.0f, box.y * 0.75f + 300.0f * 0.25f - 10.0f, box.w * 0.75f, box.h * 0.75f};
        const DrawCall* call = CallAt(expected);
        ASSERT_NE(call, nullptr);
        EXPECT_EQ(call->color.a, kAuthoredAlpha);
        EXPECT_EQ(_renderer->GetViewTransform().scale, 1.0f);
    }

    TEST_F(UICanvasTransitionTest, AModalCanvasReleasesTheScreenTheMomentItStartsToLeave) {
        ActionCounter actions;
        EXPECT_TRUE(_dialog->TakesInput());
        Click(Get(*_below, "BelowButton"));
        EXPECT_EQ(actions.below, 0) << "a shown modal shields the canvas below";
        SetDialogVisible(false);
        EXPECT_FALSE(_dialog->TakesInput()) << "before the next frame";
        Click(Get(*_below, "BelowButton"));
        EXPECT_EQ(actions.below, 1);
        Draw();
        Wait(0.25);
        ASSERT_TRUE(CallAt(Get(*_dialog, "Box").rect));
        EXPECT_FALSE(_dialog->TakesInput()) << "it is still drawn";
        Click(Get(*_below, "BelowButton"));
        EXPECT_EQ(actions.below, 2);
        Click(Get(*_dialog, "DialogButton"));
        EXPECT_EQ(actions.dialog, 0);
    }

    TEST_F(UICanvasTransitionTest, WithTransitionInputALeavingModalKeepsTheScreenUntilItEnds) {
        ActionCounter actions;
        _dialog->_transitionInput = true;
        SetDialogVisible(false);
        Draw();
        Wait(0.25);
        EXPECT_TRUE(_dialog->TakesInput());
        Click(Get(*_below, "BelowButton"));
        EXPECT_EQ(actions.below, 0) << "the modal still shields";
        Click(Get(*_dialog, "DialogButton"));
        EXPECT_EQ(actions.dialog, 1);
        Wait(1.0);
        EXPECT_FALSE(_dialog->TakesInput());
        Click(Get(*_below, "BelowButton"));
        EXPECT_EQ(actions.below, 1) << "released when the hide ends";
    }

    TEST_F(UICanvasTransitionTest, ACanvasThatIsShowingConsumesTheEventsAndDeliversNoneUnlessItTakesInput) {
        ActionCounter actions;
        SetDialogVisible(false);
        Draw();
        Wait(1.0);
        SetDialogVisible(true);
        Draw();
        Wait(0.25);
        UIElement& button = Get(*_dialog, "DialogButton");
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseMotion(Motion(button.rect.x + 5.0f, button.rect.y + 5.0f)));
        EXPECT_FALSE(button.hovered);
        Click(button);
        Click(Get(*_below, "BelowButton"));
        EXPECT_EQ(actions.dialog, 0);
        EXPECT_EQ(actions.below, 0) << "the modal shields while it comes in";
        Wait(1.0);
        Click(button);
        EXPECT_EQ(actions.dialog, 1);

        _dialog->_transitionInput = true;
        SetDialogVisible(false);
        Draw();
        Wait(1.0);
        SetDialogVisible(true);
        Draw();
        Wait(0.25);
        Click(button);
        EXPECT_EQ(actions.dialog, 2) << "with the flag it is as live as a shown one";
    }

    TEST_F(UICanvasTransitionTest, TheTooltipIsHiddenWhileTheCanvasTransitions) {
        const auto plateDrawn = [this] {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && ColorsEqual(call.color, UITooltip::kPlateColor)) {
                    return true;
                }
            }
            return false;
        };
        _dialog->_transitionInput = true;
        _dialog->_transitionHideDuration = 8.0f;
        UIElement& button = Get(*_dialog, "DialogButton");
        button.AddComponent<UITooltipComponent>().text = "Dialog tip";
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(button.rect.x + 5.0f, button.rect.y + 5.0f));
        Wait(UITooltip::kHoverDelay + kStep);
        EXPECT_TRUE(plateDrawn()) << "a steady canvas shows it";
        SetDialogVisible(false);
        Draw();
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(button.rect.x + 6.0f, button.rect.y + 6.0f));
        Wait(UITooltip::kHoverDelay + kStep);
        ASSERT_TRUE(CallAt(Get(*_dialog, "Box").rect)) << "it is still on its way out";
        EXPECT_FALSE(plateDrawn());
    }

}  // namespace BECore::Tests
