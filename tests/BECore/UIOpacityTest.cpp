#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UITooltip.h>
#include <BECore/UI/UITooltipComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SubscriptionHolder.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The opacity of an element and of a canvas: paint only, multiplied into the alpha of every draw of the subtree (a per-draw multiply, `IRenderer::SetOpacity`). The renderer's own rule is
     * `RendererPrimitivesTest`; this is what the canvas draws with it.
     */

    namespace {

        // Hud (output 800 x 600, scale 1, the colours have alpha 200 so every factor below gives a whole number):
        //   Plain   (10, 10, 100 x 40)   opacity 1: the colour as authored
        //   Half    (10, 60, 100 x 40)   opacity 0.5, with the child HalfKid (opacity 1)
        //   Outer   (10, 110, 100 x 40)  opacity 0.5, with the child Inner (opacity 0.5) and the child Whole (opacity 1)
        //   Zero    (10, 160, 100 x 40)  opacity 0, with a button ZeroButton inside
        //   Wild    (10, 210, 100 x 40)  opacity 2 in the file; Negative (10, 260, 100 x 40) opacity -1
        //   Tip     (600, 10, 48 x 48)   a tooltip
        constexpr const char* kOpacityXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize" opacity="1">
        <elements>
          <element type="UIElement" name="Plain" x="10" y="10" width="100" height="40" color="100,110,120,200"/>
          <element type="UIElement" name="Half" x="10" y="60" width="100" height="40" color="100,110,120,200" opacity="0.5">
            <children><element type="UIElement" name="HalfKid" x="0" y="0" width="50" height="40" color="10,20,30,200"/></children>
          </element>
          <element type="UIElement" name="Outer" x="10" y="110" width="100" height="40" color="100,110,120,200" opacity="0.5">
            <children>
              <element type="UIElement" name="Inner" x="0" y="0" width="50" height="40" color="10,20,30,200" opacity="0.5"/>
              <element type="UIElement" name="Whole" x="50" y="0" width="50" height="40" color="40,50,60,200"/>
            </children>
          </element>
          <element type="UIElement" name="Zero" x="10" y="160" width="100" height="40" color="100,110,120,200" opacity="0">
            <children>
              <element type="UIElement" name="ZeroButton" x="0" y="0" width="60" height="40" color="10,20,30,200">
                <components><component type="UIInputClicker" action="OpacityZero"/></components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Wild" x="10" y="210" width="100" height="40" color="100,110,120,200" opacity="2"/>
          <element type="UIElement" name="Negative" x="10" y="260" width="100" height="40" color="100,110,120,200" opacity="-1"/>
          <element type="UIElement" name="Tip" x="600" y="10" width="48" height="48" color="30,30,30,255">
            <components>
              <component type="UIInputClicker" action="OpacityTip"/>
              <component type="UITooltipComponent" text="Settings"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr double kStep = 0.125;  // an exact binary fraction; a frame is clamped to 0.25 s anyway
        constexpr uint8_t kAuthoredAlpha = 200;

        class ActionCounter : public SubscriptionHolder {
        public:
            ActionCounter() {
                Subscribe<InputEvents::ActionEvent, &ActionCounter::OnAction>(this);
            }
            int count = 0;

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                count += event.action == PoolString::Intern("OpacityZero") ? 1 : 0;
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

    }  // namespace

    class UIOpacityTest : public ::testing::Test {
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
            ASSERT_TRUE(deserializer.LoadFromString(kOpacityXml));
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

        UIElement& Get(const char* name) const {
            UIElement* element = _hud->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            return *element;
        }

        /// The alpha of the one Rect call of the last frame that has the position and the size of @p name (the elements of the screen differ in size where they share a corner)
        Optional<uint8_t> AlphaOf(const char* name) const {
            const Rect rect = Get(name).rect;
            Optional<uint8_t> alpha;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && std::abs(call.rect.x - rect.x) < 1e-3f && std::abs(call.rect.y - rect.y) < 1e-3f && std::abs(call.rect.w - rect.w) < 1e-3f) {
                    EXPECT_FALSE(alpha.has_value()) << name << " is drawn once";
                    alpha = call.color.a;
                }
            }
            return alpha;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _hud;
    };

    TEST_F(UIOpacityTest, AnElementAtFullOpacityDrawsItsColourAsAuthored) {
        EXPECT_EQ(AlphaOf("Plain"), kAuthoredAlpha);
        EXPECT_EQ(_hud->_opacity, 1.0f);
        EXPECT_EQ(Get("Plain").opacity, 1.0f);
    }

    TEST_F(UIOpacityTest, ASubtreeIsDrawnAtTheOpacityOfItsRoot) {
        EXPECT_EQ(AlphaOf("Half"), 100);
        EXPECT_EQ(AlphaOf("HalfKid"), 100) << "the child has opacity 1 of its own and still follows the parent";
    }

    TEST_F(UIOpacityTest, ANestedOpacityMultiplies) {
        EXPECT_EQ(AlphaOf("Inner"), 50) << "0.5 of the parent times 0.5 of its own";
        EXPECT_EQ(AlphaOf("Whole"), 100) << "only the parent's 0.5";
    }

    TEST_F(UIOpacityTest, TheOpacityIsPutBackAfterTheSubtree) {
        EXPECT_EQ(_renderer->GetOpacity(), 1.0f);
        EXPECT_EQ(AlphaOf("Wild"), kAuthoredAlpha) << "a sibling after faded ones is not faded";
    }

    TEST_F(UIOpacityTest, ANumberOutsideTheRangeIsCutOnLoad) {
        EXPECT_EQ(Get("Wild").opacity, 1.0f);
        EXPECT_EQ(Get("Negative").opacity, 0.0f);
        EXPECT_EQ(AlphaOf("Wild"), kAuthoredAlpha);
        EXPECT_EQ(AlphaOf("Negative"), 0);
    }

    TEST_F(UIOpacityTest, ACanvasOpacityMultipliesWithTheElementsOwn) {
        _hud->_opacity = 0.5f;
        Draw();
        EXPECT_EQ(AlphaOf("Plain"), 100);
        EXPECT_EQ(AlphaOf("Half"), 50);
        EXPECT_EQ(_renderer->GetOpacity(), 1.0f);
        _hud->_opacity = 2.0f;
        Draw();
        EXPECT_EQ(AlphaOf("Plain"), kAuthoredAlpha) << "cut to 1";
    }

    TEST_F(UIOpacityTest, OpacityIsPaintOnly) {
        // At 0 the element keeps its rect, is hit and is found: nothing but the alpha changed
        const Rect rect = Get("Zero").rect;
        EXPECT_FALSE(rect.IsEmpty());
        EXPECT_EQ(AlphaOf("Zero"), 0);
        EXPECT_EQ(_hud->GetLayout().HitTest(rect.x + 5.0f, rect.y + 5.0f), &Get("ZeroButton"));

        ActionCounter actions;
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(rect.x + 5.0f, rect.y + 5.0f));
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(rect.x + 5.0f, rect.y + 5.0f, true));
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(rect.x + 5.0f, rect.y + 5.0f, false));
        EXPECT_EQ(actions.count, 1);
    }

    TEST_F(UIOpacityTest, TheTooltipPlateIsNotFadedByTheCanvas) {
        _hud->_opacity = 0.5f;
        const Rect tip = Get("Tip").rect;
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(tip.x + 5.0f, tip.y + 5.0f));
        for (double passed = 0.0; passed < UITooltip::kHoverDelay + kStep; passed += kStep) {
            CoreManager::GetTime().Advance(kStep);
            Draw();
        }
        bool plate = false;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Rect && ColorsEqual(call.color, UITooltip::kPlateColor)) {
                plate = true;
            }
        }
        EXPECT_TRUE(plate) << "the plate is drawn in its own colour, alpha included";
        EXPECT_EQ(AlphaOf("Plain"), 100) << "while the tree is faded";
    }

}  // namespace BECore::Tests
