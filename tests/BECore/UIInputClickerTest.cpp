#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SubscriptionHolder.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Screen units equal pixels (800 x 600). Every element but Plain has a UIInputClicker:
        //   Box      (10, 10, 100 x 40)    action Boxed, value v1; the four fills of the states
        //   Pic      (10, 100, 120 x 40)   an image component whose square picture is fitted in the middle; the state tints
        //   PicOff   (10, 150, 120 x 40)   the same, the clicker is disabled
        //   Cap      (10, 200, 200 x 30)   a text component with the colours of the states
        //   Off      (10, 300, 100 x 40)   disabled
        //   Grouped  (10, 350, 100 x 40)   enabled; the tests set `groupDisabled`
        //   Hide     (300, 10, 100 x 40)   the tests hide it
        //   NoAction (300, 100, 100 x 40)  no action
        //   Plain    (300, 200, 100 x 40)  a panel with no clicker
        constexpr const char* kClickerXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Box" x="10" y="10" width="100" height="40" color="40,40,40,255" hoverColor="60,70,80,255"
                   pressedColor="20,30,40,255" disabledColor="90,91,92,255">
            <components><component type="UIInputClicker" action="Boxed" value="v1"/></components>
          </element>
          <element type="UIElement" name="Pic" x="10" y="100" width="120" height="40">
            <components>
              <component type="UIInputClicker" action="PicAction"/>
              <component type="UIImageComponent" tint="255,255,255,255" hoverTint="200,210,220,255" pressedTint="100,110,120,255" disabledTint="50,60,70,255"/>
            </components>
          </element>
          <element type="UIElement" name="PicOff" x="10" y="150" width="120" height="40">
            <components>
              <component type="UIInputClicker" action="PicOffAction" disabled="true"/>
              <component type="UIImageComponent" tint="255,255,255,255" disabledTint="50,60,70,255"/>
            </components>
          </element>
          <element type="UIElement" name="Cap" x="10" y="200" width="200" height="30" fontSize="24">
            <components>
              <component type="UIInputClicker" action="CapAction"/>
              <component type="UITextComponent" text="Score" textColor="200,100,50,255" hoverTextColor="10,20,30,255" pressedTextColor="40,50,60,255"
                         disabledTextColor="70,80,90,255" align="Center" verticalAlign="Center"/>
            </components>
          </element>
          <element type="UIElement" name="Off" x="10" y="300" width="100" height="40" color="40,40,40,255" disabledColor="90,91,92,255">
            <components><component type="UIInputClicker" action="OffAction" disabled="true"/></components>
          </element>
          <element type="UIElement" name="Grouped" x="10" y="350" width="100" height="40" color="40,40,40,255" disabledColor="90,91,92,255">
            <components><component type="UIInputClicker" action="GroupedAction"/></components>
          </element>
          <element type="UIElement" name="Hide" x="300" y="10" width="100" height="40" color="40,40,40,255">
            <components><component type="UIInputClicker" action="HideAction"/></components>
          </element>
          <element type="UIElement" name="NoAction" x="300" y="100" width="100" height="40" color="40,40,40,255">
            <components><component type="UIInputClicker"/></components>
          </element>
          <element type="UIElement" name="Plain" x="300" y="200" width="100" height="40" color="40,40,40,255" hoverColor="60,70,80,255" blocksInput="true"/>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr uint32_t kPictureSide = 32;
        constexpr float kTextRowTop = 195.0f;  // the glyphs of Cap are the textured draws from here down; the pictures are above
        constexpr float kTextRowBottom = 240.0f;
        constexpr float kEpsilon = 1e-3f;

        const Color kBoxColor{40, 40, 40, 255};
        const Color kBoxHover{60, 70, 80, 255};
        const Color kBoxPressed{20, 30, 40, 255};
        const Color kBoxDisabled{90, 91, 92, 255};

        uint32_t Pack(const Color& color) {
            return (static_cast<uint32_t>(color.r) << 24) | (static_cast<uint32_t>(color.g) << 16) | (static_cast<uint32_t>(color.b) << 8) | color.a;
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

        SDL_MouseMotionEvent Motion(float x, float y, bool held) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.x = x;
            event.y = y;
            event.state = held ? SDL_BUTTON_LMASK : 0;
            return event;
        }

        struct Emitted {
            PoolString action;
            PoolString value;
        };

        class ActionRecorder : public SubscriptionHolder {
        public:
            ActionRecorder() {
                Subscribe<InputEvents::ActionEvent, &ActionRecorder::OnAction>(this);
            }
            [[nodiscard]] const eastl::vector<Emitted>& All() const {
                return _actions;
            }
            [[nodiscard]] size_t Total() const {
                return _actions.size();
            }

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                _actions.push_back({event.action, event.value});
            }
            eastl::vector<Emitted> _actions;
        };

    }  // namespace

    class UIInputClickerTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetFontAtlasLibrary().Clear();
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kClickerXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _manager->SetActiveScene(PoolString::Intern("Main"));
            _canvas = _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);

            const eastl::vector<uint8_t> pixels(static_cast<size_t>(kPictureSide) * kPictureSide * 4, 200);
            _texture = _renderer->CreateTexture(pixels.data(), kPictureSide, kPictureSide);
            Get("Pic").GetComponent<UIImageComponent>()->GetSprite().SetTexture(_texture);
            Get("PicOff").GetComponent<UIImageComponent>()->GetSprite().SetTexture(_texture);
            Draw();
        }

        void TearDown() override {
            _texture.Reset();
            _canvas.Reset();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        void Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        UIElement& Get(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            return *element;
        }

        static Vec2 CenterOf(const UIElement& element) {
            return {element.rect.x + element.rect.w * 0.5f, element.rect.y + element.rect.h * 0.5f};
        }

        static bool MoveTo(float x, float y, bool held = false) {
            return CoreManager::GetUIInput().DispatchMouseMotion(Motion(x, y, held));
        }
        static bool Press(float x, float y) {
            return CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));
        }
        static bool Release(float x, float y) {
            return CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
        }

        /// Hovers the middle of @p element and draws
        void Hover(const UIElement& element) {
            const Vec2 at = CenterOf(element);
            MoveTo(at.x, at.y);
            Draw();
        }

        /// Hovers, presses and draws: the element is held
        void Hold(const UIElement& element) {
            const Vec2 at = CenterOf(element);
            MoveTo(at.x, at.y);
            Press(at.x, at.y);
            Draw();
        }

        /// The click of the left button on the middle of @p element
        void Click(const UIElement& element) {
            const Vec2 at = CenterOf(element);
            MoveTo(at.x, at.y);
            Press(at.x, at.y);
            Release(at.x, at.y);
        }

        /// The fill drawn for @p element in the last frame: the rect with its rect, packed; 0 when there is none
        uint32_t FillOf(const UIElement& element) const {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && std::abs(call.rect.x - element.rect.x) < kEpsilon && std::abs(call.rect.y - element.rect.y) < kEpsilon &&
                    std::abs(call.rect.w - element.rect.w) < kEpsilon && std::abs(call.rect.h - element.rect.h) < kEpsilon) {
                    return Pack(call.color);
                }
            }
            return 0;
        }

        /// The tint of the picture whose rect starts at @p top (the textured draw of the last frame there); 0 when there is none
        uint32_t PictureTint(float top) const {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && std::abs(call.rect.y - top) < kEpsilon) {
                    return Pack(call.color);
                }
            }
            return 0;
        }

        /// The colour of the last glyph of Cap: the text itself; 0 when nothing was drawn
        uint32_t TextColorDrawn() const {
            uint32_t found = 0;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.rect.y >= kTextRowTop && call.rect.y < kTextRowBottom) {
                    found = Pack(call.color);
                }
            }
            return found;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
        IntrusivePtr<ITexture> _texture;
    };

    // ---- what the clicker is --------------------------------------------------------------------------------------------------

    TEST_F(UIInputClickerTest, TheClickerComesFromTheFileWithItsFields) {
        const UIInputClicker* clicker = Get("Box").GetComponent<UIInputClicker>();
        ASSERT_NE(clicker, nullptr);
        EXPECT_EQ(clicker->action, PoolString::Intern("Boxed"));
        EXPECT_EQ(clicker->value, PoolString::Intern("v1"));
        EXPECT_FALSE(clicker->disabled);
        EXPECT_TRUE(Get("Off").GetComponent<UIInputClicker>()->disabled);
    }

    TEST_F(UIInputClickerTest, AnElementWithAClickerIsInteractiveAndAHitTargetAndAPlainPanelIsNotInteractive) {
        const Vec2 at = CenterOf(Get("Box"));
        EXPECT_TRUE(Get("Box").IsInteractive());
        EXPECT_TRUE(Get("Box").IsHitTestable());
        EXPECT_EQ(_canvas->GetLayout().HitTest(at.x, at.y), &Get("Box"));
        EXPECT_FALSE(Get("Box").interactive) << "the field is not what makes it one";

        EXPECT_FALSE(Get("Plain").IsInteractive());
        EXPECT_TRUE(Get("Plain").IsHitTestable()) << "it only blocks the input";
    }

    // ---- the click --------------------------------------------------------------------------------------------------------------

    TEST_F(UIInputClickerTest, AClickEmitsTheActionWithItsValue) {
        ActionRecorder actions;
        Click(Get("Box"));

        ASSERT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.All()[0].action, PoolString::Intern("Boxed"));
        EXPECT_EQ(actions.All()[0].value, PoolString::Intern("v1"));
    }

    TEST_F(UIInputClickerTest, AClickWithNoValueEmitsAnEmptyOne) {
        ActionRecorder actions;
        Click(Get("Pic"));

        ASSERT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.All()[0].action, PoolString::Intern("PicAction"));
        EXPECT_TRUE(actions.All()[0].value.Empty());
    }

    TEST_F(UIInputClickerTest, APressAloneEmitsNothingTheClickIsTheRelease) {
        ActionRecorder actions;
        const Vec2 at = CenterOf(Get("Box"));
        MoveTo(at.x, at.y);
        EXPECT_TRUE(Press(at.x, at.y));
        EXPECT_EQ(actions.Total(), 0u);
        EXPECT_TRUE(Release(at.x, at.y));
        EXPECT_EQ(actions.Total(), 1u);
    }

    TEST_F(UIInputClickerTest, DraggingOffTheElementCancelsTheClick) {
        ActionRecorder actions;
        const Vec2 at = CenterOf(Get("Box"));
        MoveTo(at.x, at.y);
        Press(at.x, at.y);
        MoveTo(700.0f, 500.0f, true);
        Release(700.0f, 500.0f);

        EXPECT_EQ(actions.Total(), 0u);
        EXPECT_FALSE(Get("Box").pressed);
    }

    TEST_F(UIInputClickerTest, AClickAnywhereInTheRectIsAClickOnTheElementAlsoOutsideItsPicture) {
        ActionRecorder actions;
        const UIElement& pic = Get("Pic");
        const float edge = pic.rect.x + 2.0f;  // the picture is 40 wide in the middle of 120: this point is outside it
        ASSERT_LT(edge, pic.rect.x + (pic.rect.w - pic.rect.h) * 0.5f);
        const float y = pic.rect.y + pic.rect.h * 0.5f;
        MoveTo(edge, y);
        Press(edge, y);
        Release(edge, y);

        ASSERT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.All()[0].action, PoolString::Intern("PicAction"));
    }

    TEST_F(UIInputClickerTest, AClickerWithNoActionEmitsNothingButStillTakesTheClick) {
        ActionRecorder actions;
        const Vec2 at = CenterOf(Get("NoAction"));
        MoveTo(at.x, at.y);
        EXPECT_TRUE(Press(at.x, at.y));
        EXPECT_TRUE(Release(at.x, at.y));

        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UIInputClickerTest, ADisabledClickerEmitsNothingButTakesThePressAndTheRelease) {
        ActionRecorder actions;
        const UIElement& off = Get("Off");
        EXPECT_TRUE(off.IsDisabled());
        const Vec2 at = CenterOf(off);
        MoveTo(at.x, at.y);
        EXPECT_TRUE(Press(at.x, at.y)) << "consumed: what is below does not get it";
        EXPECT_TRUE(Release(at.x, at.y));

        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UIInputClickerTest, AnElementOfADisabledGroupEmitsNothing) {
        ActionRecorder actions;
        UIElement& grouped = Get("Grouped");
        Click(grouped);
        ASSERT_EQ(actions.Total(), 1u);

        grouped.groupDisabled = true;
        EXPECT_TRUE(grouped.IsDisabled());
        Click(grouped);
        EXPECT_EQ(actions.Total(), 1u) << "the same click again emits nothing";

        grouped.groupDisabled = false;
        EXPECT_FALSE(grouped.IsDisabled());
        Click(grouped);
        EXPECT_EQ(actions.Total(), 2u);
    }

    TEST_F(UIInputClickerTest, AnElementThatTheClickerDisablesAtRunTimeStopsEmitting) {
        ActionRecorder actions;
        Get("Box").GetComponent<UIInputClicker>()->disabled = true;
        Click(Get("Box"));

        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UIInputClickerTest, AHiddenElementTakesNoClick) {
        ActionRecorder actions;
        Scene& scene = *_manager->GetSceneByName(PoolString::Intern("Main"));
        const Vec2 at = CenterOf(Get("Hide"));
        EXPECT_TRUE(UICanvasComponent::SetElementVisibleByName(scene, PoolString::Intern("Hide"), false));
        Draw();

        MoveTo(at.x, at.y);
        EXPECT_FALSE(Press(at.x, at.y));
        EXPECT_FALSE(Release(at.x, at.y));
        EXPECT_EQ(actions.Total(), 0u);
        EXPECT_EQ(_canvas->GetLayout().HitTest(at.x, at.y), nullptr);
    }

    // ---- the look follows the pointer -------------------------------------------------------------------------------------------

    TEST_F(UIInputClickerTest, TheBoxTakesTheFillOfTheStateItIsIn) {
        const UIElement& box = Get("Box");
        EXPECT_EQ(FillOf(box), Pack(kBoxColor));

        Hover(box);
        EXPECT_EQ(FillOf(box), Pack(kBoxHover));

        const Vec2 at = CenterOf(box);
        Press(at.x, at.y);
        Draw();
        EXPECT_EQ(FillOf(box), Pack(kBoxPressed));

        Release(at.x, at.y);
        Draw();
        EXPECT_EQ(FillOf(box), Pack(kBoxHover)) << "released, the pointer is still over it";

        MoveTo(700.0f, 500.0f);
        Draw();
        EXPECT_EQ(FillOf(box), Pack(kBoxColor));
    }

    TEST_F(UIInputClickerTest, APressThatMovedOffTheElementIsNotShownAsPressed) {
        const UIElement& box = Get("Box");
        Hold(box);
        ASSERT_EQ(FillOf(box), Pack(kBoxPressed));

        MoveTo(700.0f, 500.0f, true);
        Draw();
        EXPECT_EQ(FillOf(box), Pack(kBoxColor));
    }

    TEST_F(UIInputClickerTest, ADisabledElementShowsItsDisabledFillAndNoHover) {
        const UIElement& off = Get("Off");
        EXPECT_EQ(FillOf(off), Pack(kBoxDisabled));

        Hover(off);
        EXPECT_EQ(FillOf(off), Pack(kBoxDisabled));
    }

    TEST_F(UIInputClickerTest, AnElementOfADisabledGroupShowsItsDisabledFill) {
        UIElement& grouped = Get("Grouped");
        grouped.groupDisabled = true;
        Draw();

        EXPECT_EQ(FillOf(grouped), Pack(kBoxDisabled));
    }

    TEST_F(UIInputClickerTest, APanelThatOnlyBlocksInputDoesNotLightUp) {
        const UIElement& plain = Get("Plain");
        Hover(plain);

        EXPECT_TRUE(plain.hovered);
        EXPECT_EQ(FillOf(plain), Pack(kBoxColor));
    }

    TEST_F(UIInputClickerTest, ThePictureTakesTheTintOfTheState) {
        const UIElement& pic = Get("Pic");
        const float top = pic.rect.y;  // the fitted picture is as tall as the rect
        EXPECT_EQ(PictureTint(top), Pack(Color{255, 255, 255, 255}));

        Hover(pic);
        EXPECT_EQ(PictureTint(top), Pack(Color{200, 210, 220, 255}));

        const Vec2 at = CenterOf(pic);
        Press(at.x, at.y);
        Draw();
        EXPECT_EQ(PictureTint(top), Pack(Color{100, 110, 120, 255}));
    }

    TEST_F(UIInputClickerTest, ThePictureOfADisabledClickerTakesTheDisabledTint) {
        EXPECT_EQ(PictureTint(Get("PicOff").rect.y), Pack(Color{50, 60, 70, 255}));
    }

    TEST_F(UIInputClickerTest, TheTextTakesTheColourOfTheState) {
        const UIElement& cap = Get("Cap");
        ASSERT_NE(TextColorDrawn(), 0u) << "the text is drawn";
        EXPECT_EQ(TextColorDrawn(), Pack(Color{200, 100, 50, 255}));

        Hover(cap);
        EXPECT_EQ(TextColorDrawn(), Pack(Color{10, 20, 30, 255}));

        const Vec2 at = CenterOf(cap);
        Press(at.x, at.y);
        Draw();
        EXPECT_EQ(TextColorDrawn(), Pack(Color{40, 50, 60, 255}));

        Release(at.x, at.y);
        Get("Cap").groupDisabled = true;
        Draw();
        EXPECT_EQ(TextColorDrawn(), Pack(Color{70, 80, 90, 255}));
    }

}  // namespace BECore::Tests
