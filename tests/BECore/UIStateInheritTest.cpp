#include "UIComponentTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UISlider.h>
#include <BECore/UI/UIStateLook.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UIToggle.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Screen units equal pixels (800 x 600). The picture of every Icon is the same texture; the glyphs are the other textured draws.
        //   Btn      (10, 10, 200 x 60)    a clicker; an image child Icon and a text child Cap; the fills of the states
        //   Deep     (10, 100, 200 x 60)   a clicker; a plain Wrap (stretched) holds the image DeepIcon: a grandchild
        //   Dead     (10, 200, 200 x 60)   no clicker, blocksInput, hoverColor; an image child DeadIcon and a text child DeadCap
        //   Other    (300, 10, 200 x 60)   a clicker; an image child OtherIcon (the second button of a row)
        //   Off      (300, 100, 200 x 60)  a clicker, disabled; an image child OffIcon
        //   Bare     (300, 200, 200 x 60)  a clicker; a text child BareCap only, so the selected bar takes its colour
        //   Plain    (300, 300, 200 x 60)  a clicker and no children, no text: the bar is white
        //   Holder   (550, 10, 200 x 60)   a clicker; HolderButton, a UIButton with a fill per state, is its child
        //   Holder2  (550, 100, 200 x 60)  a clicker; a UIDropdown, a UIToggle and a UISlider are its children, a strip at its right edge is bare
        //   Checked  (550, 200, 200 x 60)  a clicker and a probe component that says checked; a child CheckedIcon
        constexpr const char* kInheritXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="Btn" x="10" y="10" width="200" height="60" color="40,40,40,255" hoverColor="60,70,80,255"
                   pressedColor="20,30,40,255" disabledColor="90,91,92,255" selectedColor="30,90,30,255">
            <components><component type="UIInputClicker" action="Go"/></components>
            <children>
              <element type="UIElement" name="Icon" x="0" y="10" width="40" height="40">
                <components>
                  <component type="UIImageComponent" tint="255,255,255,255" hoverTint="200,210,220,255" pressedTint="100,110,120,255" disabledTint="50,60,70,255"/>
                </components>
              </element>
              <element type="UIElement" name="Cap" x="50" y="0" width="140" height="60" fontSize="24">
                <components>
                  <component type="UITextComponent" text="Go" textColor="200,100,50,255" hoverTextColor="10,20,30,255" pressedTextColor="40,50,60,255"
                             disabledTextColor="70,80,90,255" align="Center" verticalAlign="Center"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Deep" x="10" y="100" width="200" height="60" color="40,40,40,255">
            <components><component type="UIInputClicker" action="Deeper"/></components>
            <children>
              <element type="UIElement" name="Wrap" anchor="Stretch">
                <children>
                  <element type="UIElement" name="DeepIcon" x="0" y="10" width="40" height="40">
                    <components>
                      <component type="UIImageComponent" tint="255,255,255,255" hoverTint="200,210,220,255" pressedTint="100,110,120,255" disabledTint="50,60,70,255"/>
                    </components>
                  </element>
                </children>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Dead" x="10" y="200" width="200" height="60" color="40,40,40,255" hoverColor="60,70,80,255" blocksInput="true">
            <children>
              <element type="UIElement" name="DeadIcon" x="0" y="10" width="40" height="40">
                <components>
                  <component type="UIImageComponent" tint="255,255,255,255" hoverTint="200,210,220,255" pressedTint="100,110,120,255" disabledTint="50,60,70,255"/>
                </components>
              </element>
              <element type="UIElement" name="DeadCap" x="50" y="0" width="140" height="60" fontSize="24">
                <components>
                  <component type="UITextComponent" text="Go" textColor="200,100,50,255" hoverTextColor="10,20,30,255" align="Center" verticalAlign="Center"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Other" x="300" y="10" width="200" height="60" color="40,40,40,255" hoverColor="60,70,80,255">
            <components><component type="UIInputClicker" action="Another"/></components>
            <children>
              <element type="UIElement" name="OtherIcon" x="0" y="10" width="40" height="40">
                <components>
                  <component type="UIImageComponent" tint="255,255,255,255" hoverTint="200,210,220,255" pressedTint="100,110,120,255" disabledTint="50,60,70,255"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Off" x="300" y="100" width="200" height="60" color="40,40,40,255">
            <components><component type="UIInputClicker" action="Nothing" disabled="true"/></components>
            <children>
              <element type="UIElement" name="OffIcon" x="0" y="10" width="40" height="40">
                <components>
                  <component type="UIImageComponent" tint="255,255,255,255" hoverTint="200,210,220,255" pressedTint="100,110,120,255" disabledTint="50,60,70,255"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Bare" x="300" y="200" width="200" height="60" color="40,40,40,255">
            <components><component type="UIInputClicker" action="Bare"/></components>
            <children>
              <element type="UIElement" name="BareCap" anchor="Stretch" fontSize="24">
                <components>
                  <component type="UITextComponent" text="Go" textColor="200,100,50,255" align="Center" verticalAlign="Center"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIElement" name="Plain" x="300" y="300" width="200" height="60" color="40,40,40,255">
            <components><component type="UIInputClicker" action="Plain"/></components>
          </element>
          <element type="UIElement" name="Holder" x="550" y="10" width="200" height="60" color="40,40,40,255" hoverColor="1,2,3,255" pressedColor="1,2,4,255"
                   disabledColor="1,2,5,255" selectedColor="1,2,6,255">
            <components><component type="UIInputClicker" action="Holder"/></components>
            <children>
              <element type="UIButton" name="HolderButton" x="0" y="0" width="100" height="60" text="In" color="40,40,40,255" hoverColor="60,70,80,255"
                       pressedColor="20,30,40,255" disabledColor="90,91,92,255" selectedColor="30,90,30,255"/>
            </children>
          </element>
          <element type="UIElement" name="Holder2" x="550" y="100" width="200" height="60" color="40,40,40,255">
            <components><component type="UIInputClicker" action="Holder2"/></components>
            <children>
              <element type="UIDropdown" name="HolderDrop" x="0" y="0" width="100" height="60">
                <components><component type="UITextComponent" text="Pick" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/></components>
              </element>
              <element type="UIToggle" name="HolderToggle" x="100" y="0" width="90" height="30" text="On"/>
              <element type="UISlider" name="HolderSlider" x="100" y="30" width="90" height="30"/>
            </children>
          </element>
          <element type="UIElement" name="Checked" x="550" y="200" width="200" height="60" color="40,40,40,255">
            <components>
              <component type="UIInputClicker" action="Checked"/>
              <component type="UITestProbe" tag="on" checked="true"/>
            </components>
            <children>
              <element type="UIElement" name="CheckedIcon" x="0" y="10" width="40" height="40"/>
            </children>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr uint32_t kPictureSide = 32;
        constexpr float kEpsilon = 1e-3f;

        const Color kWhite{255, 255, 255, 255};
        const Color kIconHover{200, 210, 220, 255};
        const Color kIconPressed{100, 110, 120, 255};
        const Color kIconDisabled{50, 60, 70, 255};
        const Color kCapNormal{200, 100, 50, 255};
        const Color kCapHover{10, 20, 30, 255};
        const Color kCapPressed{40, 50, 60, 255};
        const Color kCapDisabled{70, 80, 90, 255};

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

    }  // namespace

    class UIStateInheritTest : public ::testing::Test {
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
            ASSERT_TRUE(deserializer.LoadFromString(kInheritXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _manager->SetActiveScene(PoolString::Intern("Main"));
            _canvas = _manager->GetSceneByName(PoolString::Intern("Main"))->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);

            const eastl::vector<uint8_t> pixels(static_cast<size_t>(kPictureSide) * kPictureSide * 4, 200);
            _texture = _renderer->CreateTexture(pixels.data(), kPictureSide, kPictureSide);
            for (const char* icon : {"Icon", "DeepIcon", "DeadIcon", "OtherIcon", "OffIcon"}) {
                Get(icon).GetComponent<UIImageComponent>()->GetSprite().SetTexture(_texture);
            }
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

        static void MoveTo(float x, float y, bool held = false) {
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(x, y, held));
        }

        void Hover(const UIElement& element) {
            const Vec2 at = CenterOf(element);
            MoveTo(at.x, at.y);
            Draw();
        }

        void Hold(const UIElement& element) {
            const Vec2 at = CenterOf(element);
            MoveTo(at.x, at.y);
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true));
            Draw();
        }

        static bool Inside(const Rect& outer, const Rect& inner) {
            return inner.x >= outer.x - kEpsilon && inner.y >= outer.y - kEpsilon && inner.x + inner.w <= outer.x + outer.w + kEpsilon && inner.y + inner.h <= outer.y + outer.h + kEpsilon;
        }

        /// The tint of the picture drawn inside @p element in the last frame; 0 when there is none
        uint32_t PictureTint(const UIElement& element) const {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.texture == _texture.Get() && Inside(element.rect, call.rect)) {
                    return Pack(call.color);
                }
            }
            return 0;
        }

        /// The colour of the last glyph drawn inside @p element (the textured draws that are not the picture); 0 when there is none
        uint32_t TextColorDrawn(const UIElement& element) const {
            uint32_t found = 0;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.texture != _texture.Get() && Inside(element.rect, call.rect)) {
                    found = Pack(call.color);
                }
            }
            return found;
        }

        /// The colour of the bar of 4 units along the foot of @p element; 0 when there is none
        uint32_t BarColor(const UIElement& element) const {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && std::abs(call.rect.h - UIStateLook::kSelectedBarHeight) < kEpsilon &&
                    std::abs(call.rect.y - (element.rect.y + element.rect.h - UIStateLook::kSelectedBarHeight)) < kEpsilon && std::abs(call.rect.x - element.rect.x) < kEpsilon) {
                    return Pack(call.color);
                }
            }
            return 0;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
        IntrusivePtr<ITexture> _texture;
    };

    TEST_F(UIStateInheritTest, TheChildrenOfAButtonAtRestAreInTheNormalLook) {
        EXPECT_EQ(PictureTint(Get("Icon")), Pack(kWhite));
        EXPECT_EQ(TextColorDrawn(Get("Cap")), Pack(kCapNormal));
    }

    TEST_F(UIStateInheritTest, HoveringTheButtonHoversTheLookOfItsChildren) {
        Hover(Get("Btn"));

        EXPECT_EQ(PictureTint(Get("Icon")), Pack(kIconHover));
        EXPECT_EQ(TextColorDrawn(Get("Cap")), Pack(kCapHover));
        EXPECT_FALSE(Get("Icon").hovered) << "the child is drawn so, it is not hovered";
        EXPECT_TRUE(Get("Btn").hovered);
    }

    TEST_F(UIStateInheritTest, APressOnTheButtonPressesTheLookOfItsChildren) {
        Hold(Get("Btn"));

        EXPECT_EQ(PictureTint(Get("Icon")), Pack(kIconPressed));
        EXPECT_EQ(TextColorDrawn(Get("Cap")), Pack(kCapPressed));
    }

    TEST_F(UIStateInheritTest, APressThatMovedOffTheButtonIsNoLongerShownOnTheChildren) {
        Hold(Get("Btn"));
        MoveTo(700.0f, 500.0f, true);
        Draw();

        EXPECT_EQ(PictureTint(Get("Icon")), Pack(kWhite));
        EXPECT_EQ(TextColorDrawn(Get("Cap")), Pack(kCapNormal));
    }

    TEST_F(UIStateInheritTest, ADisabledClickerDisablesTheLookOfTheChildren) {
        EXPECT_EQ(PictureTint(Get("OffIcon")), Pack(kIconDisabled));

        Get("Btn").GetComponent<UIInputClicker>()->disabled = true;
        Draw();
        EXPECT_EQ(PictureTint(Get("Icon")), Pack(kIconDisabled));
        EXPECT_EQ(TextColorDrawn(Get("Cap")), Pack(kCapDisabled));
    }

    TEST_F(UIStateInheritTest, TheGroupDisablingTheButtonDisablesTheLookOfTheChildren) {
        Get("Btn").groupDisabled = true;
        Draw();

        EXPECT_EQ(PictureTint(Get("Icon")), Pack(kIconDisabled));
        EXPECT_EQ(TextColorDrawn(Get("Cap")), Pack(kCapDisabled));
    }

    TEST_F(UIStateInheritTest, AChildHasTheTintAndTheColourOfTheSameComponentOnTheButtonItself) {
        // The component answers for a state; what a child is drawn with is that answer for the state of its button
        const UIImageComponent& icon = *Get("Icon").GetComponent<UIImageComponent>();
        const UITextComponent& caption = *Get("Cap").GetComponent<UITextComponent>();
        Hover(Get("Btn"));

        const UIStateLook::State hovered{true, false, false, false};
        EXPECT_EQ(PictureTint(Get("Icon")), Pack(icon.GetStateTint(hovered)));
        EXPECT_EQ(TextColorDrawn(Get("Cap")), Pack(caption.GetStateColor(hovered)));
    }

    TEST_F(UIStateInheritTest, ASelectedButtonDrawsItsBarInTheColourOfTheCaptionChild) {
        Get("Bare").selected = true;
        Get("Plain").selected = true;
        Draw();

        EXPECT_EQ(BarColor(Get("Bare")), Pack(kCapNormal));
        EXPECT_EQ(Get("Bare").GetStateAccent().r, kCapNormal.r);
        EXPECT_EQ(BarColor(Get("Plain")), Pack(kWhite)) << "no caption anywhere: the default accent";
    }

    TEST_F(UIStateInheritTest, ASelectedButtonIsSelectedForItsChildrenToo) {
        Get("Btn").selected = true;
        Draw();

        EXPECT_TRUE(Get("Icon").GetVisualState().selected);
        EXPECT_FALSE(Get("Dead").GetVisualState().selected);
        EXPECT_EQ(BarColor(Get("Btn")), Pack(kCapNormal)) << "the accent is the caption child's, as if the button had the text";
    }

    TEST_F(UIStateInheritTest, AGrandchildFollowsTheButtonToo) {
        Hover(Get("Deep"));

        EXPECT_EQ(PictureTint(Get("DeepIcon")), Pack(kIconHover));
        Hold(Get("Deep"));
        EXPECT_EQ(PictureTint(Get("DeepIcon")), Pack(kIconPressed));
    }

    TEST_F(UIStateInheritTest, TheChildrenOfAnElementWithNoClickerAreNeverTintedByAState) {
        Hover(Get("Dead"));

        ASSERT_TRUE(Get("Dead").hovered) << "the panel is hovered (it blocks the input)";
        EXPECT_EQ(PictureTint(Get("DeadIcon")), Pack(kWhite));
        EXPECT_EQ(TextColorDrawn(Get("DeadCap")), Pack(kCapNormal));
        EXPECT_FALSE(Get("DeadIcon").GetVisualState().hovered);
    }

    TEST_F(UIStateInheritTest, TwoButtonsSideBySideDoNotShareTheirState) {
        Hover(Get("Btn"));

        EXPECT_EQ(PictureTint(Get("Icon")), Pack(kIconHover));
        EXPECT_EQ(PictureTint(Get("OtherIcon")), Pack(kWhite));

        Hover(Get("Other"));
        EXPECT_EQ(PictureTint(Get("Icon")), Pack(kWhite));
        EXPECT_EQ(PictureTint(Get("OtherIcon")), Pack(kIconHover));
    }

    TEST_F(UIStateInheritTest, TheOwnFlagsOfAnElementAndOfItsChildrenAreNotChanged) {
        Hover(Get("Btn"));

        EXPECT_FALSE(Get("Icon").hovered);
        EXPECT_FALSE(Get("Cap").pressed);
        EXPECT_FALSE(Get("Icon").IsDisabled()) << "disabled is the child's own answer; the look is what follows the button";
        EXPECT_FALSE(Get("Icon").IsInteractive());
        EXPECT_TRUE(Get("Btn").GetVisualState().hovered);
    }

    TEST_F(UIStateInheritTest, AnElementOutsideAnyButtonKeepsItsOwnFlagsAfterTheButtonLostTheState) {
        Hover(Get("Btn"));
        MoveTo(700.0f, 500.0f);
        Draw();

        EXPECT_EQ(PictureTint(Get("Icon")), Pack(kWhite));
        EXPECT_FALSE(Get("Icon").GetVisualState().hovered);
    }

    TEST_F(UIStateInheritTest, AUIButtonBelowAnElementWithAClickerDrawsItsOwnState) {
        auto& holder = Get("Holder");
        auto& button = *Get("HolderButton").Cast<UIButton>();
        ASSERT_TRUE(button.OwnsState());
        EXPECT_FALSE(Get("Icon").OwnsState());
        EXPECT_TRUE(holder.OwnsState());

        // The pointer is over the holder but not over the button: the button is at rest
        MoveTo(holder.rect.x + 150.0f, holder.rect.y + 30.0f);
        Draw();
        ASSERT_TRUE(holder.hovered);
        ASSERT_FALSE(button.hovered);
        EXPECT_FALSE(button.GetVisualState().hovered);
        EXPECT_EQ(Pack(button.GetCurrentFill()), Pack(Color{40, 40, 40, 255}));

        // Over the button both are hovered: its own look, not the holder's fill
        Hover(button);
        EXPECT_TRUE(button.GetVisualState().hovered);
        EXPECT_EQ(Pack(button.GetCurrentFill()), Pack(Color{60, 70, 80, 255}));

        Hold(button);
        EXPECT_TRUE(button.GetVisualState().pressed);
        EXPECT_EQ(Pack(button.GetCurrentFill()), Pack(Color{20, 30, 40, 255}));
        MoveTo(700.0f, 500.0f);
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(700.0f, 500.0f, false));
        Draw();

        button.disabled = true;
        Draw();
        EXPECT_TRUE(button.GetVisualState().disabled);
        EXPECT_EQ(Pack(button.GetCurrentFill()), Pack(Color{90, 91, 92, 255}));
        button.disabled = false;

        button.selected = true;
        Draw();
        EXPECT_TRUE(button.GetVisualState().selected);
        EXPECT_FALSE(holder.GetVisualState().selected) << "the holder's flags are its own";
        EXPECT_EQ(Pack(button.GetCurrentFill()), Pack(Color{30, 90, 30, 255}));
    }

    TEST_F(UIStateInheritTest, ADisabledAncestorDoesNotDisableAUIButton) {
        auto& holder = Get("Holder");
        auto& button = *Get("HolderButton").Cast<UIButton>();
        holder.GetComponent<UIInputClicker>()->disabled = true;
        Draw();

        EXPECT_TRUE(holder.GetVisualState().disabled);
        EXPECT_FALSE(button.GetVisualState().disabled) << "an ancestor's state is not the button's";
    }

    TEST_F(UIStateInheritTest, ADropdownAToggleAndASliderBelowAnElementWithAClickerAreInTheirOwnState) {
        auto& holder = Get("Holder2");
        for (const char* name : {"HolderDrop", "HolderToggle", "HolderSlider"}) {
            EXPECT_TRUE(Get(name).OwnsState()) << name;
        }

        // The pointer is over the holder and over none of the three: each is at rest
        MoveTo(holder.rect.x + 195.0f, holder.rect.y + 59.0f);
        Draw();
        ASSERT_TRUE(holder.hovered);
        for (const char* name : {"HolderDrop", "HolderToggle", "HolderSlider"}) {
            EXPECT_FALSE(Get(name).GetVisualState().hovered) << name;
        }

        Hover(Get("HolderDrop"));
        EXPECT_TRUE(Get("HolderDrop").GetVisualState().hovered);
        EXPECT_FALSE(Get("HolderToggle").GetVisualState().hovered);
        Hold(Get("HolderDrop"));
        EXPECT_TRUE(Get("HolderDrop").GetVisualState().pressed);
        EXPECT_FALSE(Get("HolderSlider").GetVisualState().pressed);
    }

    TEST_F(UIStateInheritTest, TheDisabledOfTheHolderIsNotTheDisabledOfADropdownAToggleOrASliderAndTheirOwnIs) {
        auto& holder = Get("Holder2");
        holder.GetComponent<UIInputClicker>()->disabled = true;
        Draw();

        ASSERT_TRUE(holder.IsDisabled());
        EXPECT_FALSE(Get("HolderDrop").GetVisualState().disabled);
        EXPECT_FALSE(Get("HolderToggle").GetVisualState().disabled);
        EXPECT_FALSE(Get("HolderSlider").GetVisualState().disabled);

        Get("HolderDrop").Cast<UIDropdown>()->disabled = true;
        Get("HolderToggle").Cast<UIToggle>()->disabled = true;
        Get("HolderSlider").Cast<UISlider>()->disabled = true;
        for (const char* name : {"HolderDrop", "HolderToggle", "HolderSlider"}) {
            EXPECT_TRUE(Get(name).IsDisabled()) << name;
            EXPECT_TRUE(Get(name).GetVisualState().disabled) << name;
        }
        Get("HolderToggle").groupDisabled = true;
        Get("HolderToggle").Cast<UIToggle>()->disabled = false;
        EXPECT_TRUE(Get("HolderToggle").IsDisabled()) << "the group's too";
    }

    TEST_F(UIStateInheritTest, ACheckedComponentGivesCheckedToTheOwnerAndItsChildren) {
        EXPECT_TRUE(Get("Checked").GetVisualState().checked);
        EXPECT_TRUE(Get("CheckedIcon").GetVisualState().checked) << "handed down like hovered";
        EXPECT_FALSE(Get("Btn").GetVisualState().checked);
        EXPECT_FALSE(Get("Icon").GetVisualState().checked);
        EXPECT_FALSE(Get("Holder").GetVisualState().checked);
    }

    TEST_F(UIStateInheritTest, CheckedFollowsTheComponentAtRunTimeAndDoesNotTouchTheOtherFlags) {
        auto* probe = Get("Checked").GetComponent<UITestProbe>();
        ASSERT_NE(probe, nullptr);
        probe->checked = false;
        Draw();

        const UIStateLook::State state = Get("CheckedIcon").GetVisualState();
        EXPECT_FALSE(state.checked);
        EXPECT_FALSE(state.hovered || state.pressed || state.disabled || state.selected);
        EXPECT_FALSE(Get("Checked").GetVisualState().checked);
    }

}  // namespace BECore::Tests
