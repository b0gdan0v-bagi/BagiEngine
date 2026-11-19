#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Common/UIInputEvents.h>
#include <Common/UITestSupport.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
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
        //   Still    (300, 300, 100 x 40)  `stateLook="false"`: the fills of the states are given and never drawn
        //   StillOff (300, 350, 100 x 40)  `stateLook="false"` and disabled
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
          <element type="UIElement" name="Still" x="300" y="300" width="100" height="40" color="40,40,40,255" hoverColor="60,70,80,255" pressedColor="20,30,40,255"
                   disabledColor="90,91,92,255" selectedColor="30,90,30,255">
            <components><component type="UIInputClicker" action="StillAction" stateLook="false"/></components>
          </element>
          <element type="UIElement" name="StillOff" x="300" y="350" width="100" height="40" color="40,40,40,255" disabledColor="90,91,92,255">
            <components><component type="UIInputClicker" action="StillOffAction" stateLook="false" disabled="true"/></components>
          </element>
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

    }  // namespace

    class UIInputClickerTest : public UISceneFixture {
    protected:
        void SetUp() override {
            UISceneFixture::SetUp();
            ASSERT_NO_FATAL_FAILURE(LoadScene(kClickerXml));
            _manager->SetActiveScene("Main"_intern);
            _canvas = _manager->GetSceneByName("Main"_intern)->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);

            const Vector<uint8_t> pixels(static_cast<size_t>(kPictureSide) * kPictureSide * 4, 200);
            _texture = _renderer->CreateTexture(pixels.data(), kPictureSide, kPictureSide);
            Require(El("Pic").GetComponent<UIImageComponent>()).GetSprite().SetTexture(_texture);
            Require(El("PicOff").GetComponent<UIImageComponent>()).GetSprite().SetTexture(_texture);
            Draw();
        }

        void TearDown() override {
            _texture.Reset();
            UISceneFixture::TearDown();
        }

        void Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        static Vec2 CenterOf(const UIElement& element) {
            return {element.rect.x + element.rect.w * 0.5f, element.rect.y + element.rect.h * 0.5f};
        }

        static bool MoveTo(float x, float y, bool held = false) {
            return CoreManager::GetUIInput().DispatchMouseMotion(Motion(x, y, held ? SDL_BUTTON_LMASK : 0));
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

        /// The fill drawn for @p element in the last frame: the rect with its rect; all zero when there is none
        Color FillOf(const UIElement& element) const {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && std::abs(call.rect.x - element.rect.x) < kEpsilon && std::abs(call.rect.y - element.rect.y) < kEpsilon &&
                    std::abs(call.rect.w - element.rect.w) < kEpsilon && std::abs(call.rect.h - element.rect.h) < kEpsilon) {
                    return call.color;
                }
            }
            return Color{};
        }

        /// The tint of the picture whose rect starts at @p top (the textured draw of the last frame there); all zero when there is none
        Color PictureTint(float top) const {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && std::abs(call.rect.y - top) < kEpsilon) {
                    return call.color;
                }
            }
            return Color{};
        }

        /// The colour of the last glyph of Cap: the text itself; all zero when nothing was drawn
        Color TextColorDrawn() const {
            Color found{};
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.rect.y >= kTextRowTop && call.rect.y < kTextRowBottom) {
                    found = call.color;
                }
            }
            return found;
        }

        IntrusivePtr<ITexture> _texture;
    };

    // ---- what the clicker is --------------------------------------------------------------------------------------------------

    TEST_F(UIInputClickerTest, TheClickerComesFromTheFileWithItsFields) {
        const UIInputClicker* clicker = El("Box").GetComponent<UIInputClicker>();
        ASSERT_NE(clicker, nullptr);
        EXPECT_EQ(clicker->action, "Boxed"_intern);
        EXPECT_EQ(clicker->value, "v1"_intern);
        EXPECT_FALSE(clicker->disabled);
        EXPECT_TRUE(Require(El("Off").GetComponent<UIInputClicker>()).disabled);
    }

    TEST_F(UIInputClickerTest, AnElementWithAClickerIsInteractiveAndAHitTargetAndAPlainPanelIsNotInteractive) {
        const Vec2 at = CenterOf(El("Box"));
        EXPECT_TRUE(El("Box").IsInteractive());
        EXPECT_TRUE(El("Box").IsHitTestable());
        EXPECT_EQ(_canvas->GetLayout().HitTest(at.x, at.y), &El("Box"));
        EXPECT_FALSE(El("Box").interactive) << "the field is not what makes it one";

        EXPECT_FALSE(El("Plain").IsInteractive());
        EXPECT_TRUE(El("Plain").IsHitTestable()) << "it only blocks the input";
    }

    // ---- the click --------------------------------------------------------------------------------------------------------------

    TEST_F(UIInputClickerTest, AClickEmitsTheActionWithItsValue) {
        ActionLog actions;
        Click(El("Box"));

        ASSERT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.All()[0].action, "Boxed"_intern);
        EXPECT_EQ(actions.All()[0].value, "v1"_intern);
    }

    TEST_F(UIInputClickerTest, AClickWithNoValueEmitsAnEmptyOne) {
        ActionLog actions;
        Click(El("Pic"));

        ASSERT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.All()[0].action, "PicAction"_intern);
        EXPECT_TRUE(actions.All()[0].value.Empty());
    }

    TEST_F(UIInputClickerTest, APressAloneEmitsNothingTheClickIsTheRelease) {
        ActionLog actions;
        const Vec2 at = CenterOf(El("Box"));
        MoveTo(at.x, at.y);
        EXPECT_TRUE(Press(at.x, at.y));
        EXPECT_EQ(actions.Total(), 0u);
        EXPECT_TRUE(Release(at.x, at.y));
        EXPECT_EQ(actions.Total(), 1u);
    }

    TEST_F(UIInputClickerTest, DraggingOffTheElementCancelsTheClick) {
        ActionLog actions;
        const Vec2 at = CenterOf(El("Box"));
        MoveTo(at.x, at.y);
        Press(at.x, at.y);
        MoveTo(700.0f, 500.0f, true);
        Release(700.0f, 500.0f);

        EXPECT_EQ(actions.Total(), 0u);
        EXPECT_FALSE(El("Box").pressed);
    }

    TEST_F(UIInputClickerTest, AClickAnywhereInTheRectIsAClickOnTheElementAlsoOutsideItsPicture) {
        ActionLog actions;
        const UIElement& pic = El("Pic");
        const float edge = pic.rect.x + 2.0f;  // the picture is 40 wide in the middle of 120: this point is outside it
        ASSERT_LT(edge, pic.rect.x + (pic.rect.w - pic.rect.h) * 0.5f);
        const float y = pic.rect.y + pic.rect.h * 0.5f;
        MoveTo(edge, y);
        Press(edge, y);
        Release(edge, y);

        ASSERT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.All()[0].action, "PicAction"_intern);
    }

    TEST_F(UIInputClickerTest, AClickerWithNoActionEmitsNothingButStillTakesTheClick) {
        ActionLog actions;
        const Vec2 at = CenterOf(El("NoAction"));
        MoveTo(at.x, at.y);
        EXPECT_TRUE(Press(at.x, at.y));
        EXPECT_TRUE(Release(at.x, at.y));

        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UIInputClickerTest, ADisabledClickerEmitsNothingButTakesThePressAndTheRelease) {
        ActionLog actions;
        const UIElement& off = El("Off");
        EXPECT_TRUE(off.IsDisabled());
        const Vec2 at = CenterOf(off);
        MoveTo(at.x, at.y);
        EXPECT_TRUE(Press(at.x, at.y)) << "consumed: what is below does not get it";
        EXPECT_TRUE(Release(at.x, at.y));

        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UIInputClickerTest, AnElementOfADisabledGroupEmitsNothing) {
        ActionLog actions;
        UIElement& grouped = El("Grouped");
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
        ActionLog actions;
        Require(El("Box").GetComponent<UIInputClicker>()).disabled = true;
        Click(El("Box"));

        EXPECT_EQ(actions.Total(), 0u);
    }

    TEST_F(UIInputClickerTest, AHiddenElementTakesNoClick) {
        ActionLog actions;
        Scene& scene = *_manager->GetSceneByName("Main"_intern);
        const Vec2 at = CenterOf(El("Hide"));
        EXPECT_TRUE(UICanvasComponent::SetElementVisibleByName(scene, "Hide"_intern, false));
        Draw();

        MoveTo(at.x, at.y);
        EXPECT_FALSE(Press(at.x, at.y));
        EXPECT_FALSE(Release(at.x, at.y));
        EXPECT_EQ(actions.Total(), 0u);
        EXPECT_EQ(_canvas->GetLayout().HitTest(at.x, at.y), nullptr);
    }

    // ---- the look follows the pointer -------------------------------------------------------------------------------------------

    TEST_F(UIInputClickerTest, TheBoxTakesTheFillOfTheStateItIsIn) {
        const UIElement& box = El("Box");
        EXPECT_PRED2(ColorsEqual, FillOf(box), kBoxColor);

        Hover(box);
        EXPECT_PRED2(ColorsEqual, FillOf(box), kBoxHover);

        const Vec2 at = CenterOf(box);
        Press(at.x, at.y);
        Draw();
        EXPECT_PRED2(ColorsEqual, FillOf(box), kBoxPressed);

        Release(at.x, at.y);
        Draw();
        EXPECT_PRED2(ColorsEqual, FillOf(box), kBoxHover) << "released, the pointer is still over it";

        MoveTo(700.0f, 500.0f);
        Draw();
        EXPECT_PRED2(ColorsEqual, FillOf(box), kBoxColor);
    }

    TEST_F(UIInputClickerTest, APressThatMovedOffTheElementIsNotShownAsPressed) {
        const UIElement& box = El("Box");
        Hold(box);
        ASSERT_PRED2(ColorsEqual, FillOf(box), kBoxPressed);

        MoveTo(700.0f, 500.0f, true);
        Draw();
        EXPECT_PRED2(ColorsEqual, FillOf(box), kBoxColor);
    }

    TEST_F(UIInputClickerTest, ADisabledElementShowsItsDisabledFillAndNoHover) {
        const UIElement& off = El("Off");
        EXPECT_PRED2(ColorsEqual, FillOf(off), kBoxDisabled);

        Hover(off);
        EXPECT_PRED2(ColorsEqual, FillOf(off), kBoxDisabled);
    }

    TEST_F(UIInputClickerTest, AnElementOfADisabledGroupShowsItsDisabledFill) {
        UIElement& grouped = El("Grouped");
        grouped.groupDisabled = true;
        Draw();

        EXPECT_PRED2(ColorsEqual, FillOf(grouped), kBoxDisabled);
    }

    TEST_F(UIInputClickerTest, APanelThatOnlyBlocksInputDoesNotLightUp) {
        const UIElement& plain = El("Plain");
        Hover(plain);

        EXPECT_TRUE(plain.hovered);
        EXPECT_PRED2(ColorsEqual, FillOf(plain), kBoxColor);
    }

    TEST_F(UIInputClickerTest, WithStateLookOffTheBoxIsPlainInEveryState) {
        UIElement& still = El("Still");
        EXPECT_PRED2(ColorsEqual, FillOf(still), kBoxColor);

        Hover(still);
        ASSERT_TRUE(still.GetVisualState().hovered) << "the state is there, only the box ignores it";
        EXPECT_PRED2(ColorsEqual, FillOf(still), kBoxColor);

        const Vec2 at = CenterOf(still);
        Press(at.x, at.y);
        Draw();
        ASSERT_TRUE(still.GetVisualState().pressed);
        EXPECT_PRED2(ColorsEqual, FillOf(still), kBoxColor);
        Release(at.x, at.y);

        still.selected = true;
        Draw();
        EXPECT_PRED2(ColorsEqual, FillOf(still), kBoxColor);
        EXPECT_PRED2(ColorsEqual, FillOf(El("StillOff")), kBoxColor) << "disabled, still plain";
    }

    TEST_F(UIInputClickerTest, WithStateLookOffTheElementIsStillAClickerAndDisabledWorks) {
        EXPECT_FALSE(El("Still").GivesStateLook());
        EXPECT_TRUE(El("Still").IsInteractive());
        EXPECT_TRUE(El("Box").GivesStateLook()) << "the default is as before";
        EXPECT_TRUE(El("StillOff").IsDisabled());
        EXPECT_TRUE(El("StillOff").GetVisualState().disabled);

        ActionLog actions;
        Click(El("Still"));
        ASSERT_EQ(actions.Total(), 1u);
        EXPECT_EQ(actions.All()[0].action, "StillAction"_intern);
        Click(El("StillOff"));
        EXPECT_EQ(actions.Total(), 1u);
    }

    TEST_F(UIInputClickerTest, TheStateLookFlagRoundTripsThroughXml) {
        const UIInputClicker* clicker = El("Still").GetComponent<UIInputClicker>();
        ASSERT_NE(clicker, nullptr);
        EXPECT_FALSE(clicker->stateLook);
        EXPECT_TRUE(Require(El("Box").GetComponent<UIInputClicker>()).stateLook);

        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("element"));
        El("Still").Serialize(serializer);
        serializer.EndObject();
        const String xml = serializer.SaveToString();
        EXPECT_NE(xml.find("stateLook=\"false\""), String::npos) << xml.c_str();

        XmlSerializer plain;
        ASSERT_TRUE(plain.BeginObject("element"));
        El("Box").Serialize(plain);
        plain.EndObject();
        const String plainXml = plain.SaveToString();
        EXPECT_NE(plainXml.find("stateLook=\"true\""), String::npos) << plainXml.c_str();
    }

    TEST_F(UIInputClickerTest, ThePictureTakesTheTintOfTheState) {
        const UIElement& pic = El("Pic");
        const float top = pic.rect.y;  // the fitted picture is as tall as the rect
        EXPECT_PRED2(ColorsEqual, PictureTint(top), (Color{255, 255, 255, 255}));

        Hover(pic);
        EXPECT_PRED2(ColorsEqual, PictureTint(top), (Color{200, 210, 220, 255}));

        const Vec2 at = CenterOf(pic);
        Press(at.x, at.y);
        Draw();
        EXPECT_PRED2(ColorsEqual, PictureTint(top), (Color{100, 110, 120, 255}));
    }

    TEST_F(UIInputClickerTest, ThePictureOfADisabledClickerTakesTheDisabledTint) {
        EXPECT_PRED2(ColorsEqual, PictureTint(El("PicOff").rect.y), (Color{50, 60, 70, 255}));
    }

    TEST_F(UIInputClickerTest, TheTextTakesTheColourOfTheState) {
        const UIElement& cap = El("Cap");
        ASSERT_FALSE(ColorsEqual(TextColorDrawn(), Color{})) << "the text is drawn";
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), (Color{200, 100, 50, 255}));

        Hover(cap);
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), (Color{10, 20, 30, 255}));

        const Vec2 at = CenterOf(cap);
        Press(at.x, at.y);
        Draw();
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), (Color{40, 50, 60, 255}));

        Release(at.x, at.y);
        El("Cap").groupDisabled = true;
        Draw();
        EXPECT_PRED2(ColorsEqual, TextColorDrawn(), (Color{70, 80, 90, 255}));
    }

}  // namespace BECore::Tests
