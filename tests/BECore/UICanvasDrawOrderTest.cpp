#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Renderer/ViewTransform.h>
#include <BECore/Time/Time.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UIThemes.h>
#include <BECore/UI/UITooltip.h>
#include <BECore/UI/UITransitionComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <Common/UIInputEvents.h>
#include <Common/UITestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * What a canvas draws, call by call, for a screen that has every feature its draw core has to keep: nested opacity, hidden subtrees, show / hide transitions, canvas-level opacity and
     * transition, the state of a clicker owner handed to its children, a popup list, the tooltip plate and the theme. The expectations come from the rules (UI/CLAUDE.md, ui-transitions.md),
     * never from a run: they pin the pixels the draw core must produce however it walks the tree.
     */

    namespace {

        // A Screen canvas in window pixels (output 800 x 600, scale 1). Every colour has alpha 200, so every opacity gives a whole number (0.5 -> 100, 0.25 -> 50, 0.125 -> 25).
        // The rect of an element is unique among the drawn ones: a call is recognised by its rect.
        //   N1 (10, 10, 200 x 40) 0.5; N2 (150 wide) 0.5 inside it with N3 (100 wide) 0.5; N4 (60 wide) after N2 with opacity 1; N5 (10, 60) after N1 with opacity 1
        //   H1 (10, 110) is hidden with H2 inside; H3 (10, 160) is shown with a hidden child H4 and a shown one H5
        //   P1 (10, 210, 240 x 60) has a text of its own and the children P2 (a box), P3 (a box with a text) and P4 (a box)
        //   T1 (300, 10, 100 x 80) shows and hides in a second (Linear) scaling from 0.5 about its centre and sliding 32 down; children T2 and T3; T4 (300, 100) after it with none;
        //   T5 (300, 150, 120 x 40) has opacity 0.5 and a plain fade
        //   K1 (500, 10, 200 x 60) is a clicker with the text child K2 and the clicker K3 whose text child is K4; each has its own colour per state
        //   DGrp (500, 100, 200 x 60) has opacity 0.5 and holds the dropdown DDrop (two options DA and DB); DLater (500, 170, 170 x 40) comes after it and lies where the list opens
        //   Q1 (20, 450, 100 x 40) is a clicker with a tooltip
        //   S1 and S2 (10, 300 and 10, 350) are filled by the theme token `panel`
        constexpr const char* kScreenXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIElement" name="N1" x="10" y="10" width="200" height="40" color="100,110,120,200" opacity="0.5">
            <children>
              <element type="UIElement" name="N2" x="0" y="0" width="150" height="30" color="100,110,120,200" opacity="0.5">
                <children>
                  <element type="UIElement" name="N3" x="0" y="0" width="100" height="20" color="100,110,120,200" opacity="0.5"/>
                </children>
              </element>
              <element type="UIElement" name="N4" x="140" y="0" width="60" height="35" color="100,110,120,200"/>
            </children>
          </element>
          <element type="UIElement" name="N5" x="10" y="60" width="210" height="40" color="100,110,120,200"/>
          <element type="UIElement" name="H1" x="10" y="110" width="220" height="40" color="100,110,120,200" visible="false">
            <children>
              <element type="UIElement" name="H2" x="0" y="0" width="70" height="40" color="100,110,120,200"/>
            </children>
          </element>
          <element type="UIElement" name="H3" x="10" y="160" width="230" height="40" color="100,110,120,200">
            <children>
              <element type="UIElement" name="H4" x="0" y="0" width="80" height="40" color="100,110,120,200" visible="false"/>
              <element type="UIElement" name="H5" x="100" y="0" width="90" height="40" color="100,110,120,200"/>
            </children>
          </element>
          <element type="UIElement" name="P1" x="10" y="210" width="240" height="60" color="100,110,120,200" fontSize="16">
            <components><component type="UITextComponent" text="Ab" textColor="255,255,255,200" align="Left" verticalAlign="Top" wrap="false"/></components>
            <children>
              <element type="UIElement" name="P2" x="0" y="30" width="70" height="30" color="100,110,120,200"/>
              <element type="UIElement" name="P3" x="80" y="30" width="75" height="30" color="100,110,120,200" fontSize="16">
                <components><component type="UITextComponent" text="Cd" textColor="255,255,255,200" align="Left" verticalAlign="Center" wrap="false"/></components>
              </element>
              <element type="UIElement" name="P4" x="160" y="30" width="80" height="30" color="100,110,120,200"/>
            </children>
          </element>
          <element type="UIElement" name="S1" x="10" y="300" width="250" height="40" color="@panel"/>
          <element type="UIElement" name="S2" x="10" y="350" width="260" height="40" color="@panel"/>
          <element type="UIElement" name="T1" x="300" y="10" width="100" height="80" color="100,110,120,200">
            <components><component type="UITransitionComponent" duration="1" ease="Linear" scaleFrom="0.5" slideY="32"/></components>
            <children>
              <element type="UIElement" name="T2" x="0" y="0" width="60" height="40" color="100,110,120,200"/>
              <element type="UIElement" name="T3" x="0" y="50" width="40" height="20" color="100,110,120,200"/>
            </children>
          </element>
          <element type="UIElement" name="T4" x="300" y="100" width="110" height="40" color="100,110,120,200"/>
          <element type="UIElement" name="T5" x="300" y="150" width="120" height="40" color="100,110,120,200" opacity="0.5">
            <components><component type="UITransitionComponent" duration="1" ease="Linear"/></components>
          </element>
          <element type="UIElement" name="K1" x="500" y="10" width="200" height="60" color="40,40,40,255" hoverColor="60,70,80,255" pressedColor="20,30,40,255" disabledColor="90,91,92,255">
            <components><component type="UIInputClicker" action="DrawOrderK1"/></components>
            <children>
              <element type="UIElement" name="K2" x="0" y="0" width="60" height="60" fontSize="24">
                <components>
                  <component type="UITextComponent" text="A" textColor="200,100,50,255" hoverTextColor="10,20,30,255" pressedTextColor="40,50,60,255" disabledTextColor="70,80,90,255"
                             align="Center" verticalAlign="Center" wrap="false"/>
                </components>
              </element>
              <element type="UIElement" name="K3" x="70" y="0" width="60" height="60" color="51,52,53,255" hoverColor="71,72,73,255" pressedColor="31,32,33,255" disabledColor="81,82,83,255">
                <components><component type="UIInputClicker" action="DrawOrderK3"/></components>
                <children>
                  <element type="UIElement" name="K4" x="0" y="0" width="50" height="50" fontSize="24">
                    <components>
                      <component type="UITextComponent" text="B" textColor="111,112,113,255" hoverTextColor="121,122,123,255" pressedTextColor="131,132,133,255"
                                 disabledTextColor="141,142,143,255" align="Center" verticalAlign="Center" wrap="false"/>
                    </components>
                  </element>
                </children>
              </element>
            </children>
          </element>
          <element type="UIElement" name="DGrp" x="500" y="100" width="200" height="60" color="100,110,120,200" opacity="0.5">
            <children>
              <element type="UIDropdown" name="DDrop" bind="drawOrderChoice" x="0" y="0" width="190" height="60" optionHeight="40" color="100,110,120,200">
                <components><component type="UITextComponent" verticalAlign="Center" wrap="false"/></components>
                <children>
                  <element type="UIElement" name="DA" color="100,110,120,200"><components><component type="UIInputClicker" value="a"/></components></element>
                  <element type="UIElement" name="DB" color="100,110,120,200"><components><component type="UIInputClicker" value="b"/></components></element>
                </children>
              </element>
            </children>
          </element>
          <element type="UIElement" name="DLater" x="500" y="170" width="170" height="40" color="100,110,120,200"/>
          <element type="UIElement" name="Q1" x="20" y="450" width="100" height="40" color="30,30,30,200">
            <components>
              <component type="UIInputClicker" action="DrawOrderQ1"/>
              <component type="UITooltipComponent" text="Settings"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr double kStep = 0.125;  // an exact binary fraction; a frame is clamped to 0.25 s anyway
        // The alpha of an authored 200 after one, two and three factors of 0.5 (the progress of a Linear transition after half of its second is one of them)
        constexpr int kHalf = 100;
        constexpr int kQuarter = 50;
        constexpr int kEighth = 25;
        constexpr float kScaleAtHalf = 0.75f;
        constexpr float kSlideAtHalf = 16.0f;
        constexpr Color kLightPanel{240, 240, 230, 255};
        constexpr Color kDarkPanel{30, 32, 40, 255};
        constexpr Color kLightLine{10, 20, 30, 255};

        /// @p rect scaled by @p scale about @p pivot and moved by @p slide: what a transition does to what it draws
        Rect Transitioned(const Rect& rect, Vec2 pivot, float scale, Vec2 slide) {
            return {pivot.x + (rect.x - pivot.x) * scale + slide.x, pivot.y + (rect.y - pivot.y) * scale + slide.y, rect.w * scale, rect.h * scale};
        }

        Vec2 CentreOf(const Rect& rect) {
            return {rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
        }

        String ColorText(const Color& color) {
            return Format("{},{},{},{}", static_cast<int>(color.r), static_cast<int>(color.g), static_cast<int>(color.b), static_cast<int>(color.a));
        }

        bool EndsWith(const String& text, StringView tail) {
            return StringView(text.c_str(), text.size()).ends_with(tail);
        }

    }  // namespace

    class UICanvasDrawOrderTest : public UISceneFixture {
    protected:
        enum class Show { Alpha, Colour };

        void SetUp() override {
            UISceneFixture::SetUp();
            CoreManager::GetTime().Reset();
            UIThemes& themes = CoreManager::GetUIThemes();
            themes.Set("light"_intern, "panel", kLightPanel);
            themes.Set("light"_intern, "line", kLightLine);
            themes.Set("dark"_intern, "panel", kDarkPanel);
            themes.Set("dark"_intern, "line", kDarkPanel);
            ASSERT_NO_FATAL_FAILURE(LoadScene(kScreenXml));
            ASSERT_TRUE(_canvas);
            Draw();
        }

        void TearDown() override {
            CoreManager::GetTime().Reset();
            UISceneFixture::TearDown();
        }

        /// Lets @p seconds of game time pass in frames of `kStep`; the calls of the last frame stay
        void Wait(double seconds) {
            for (double passed = 0.0; passed < seconds - 1e-9; passed += kStep) {
                CoreManager::GetTime().Advance(kStep);
                Draw();
            }
        }

        void Hide(const char* name) {
            ASSERT_TRUE(_canvas->SetElementVisible(PoolString::Intern(name), false));
        }

        static void CollectDrawn(const UIElement& element, Vector<const UIElement*>& out) {
            for (const auto& child : element.children) {
                if (!child->rect.IsEmpty()) {
                    out.push_back(child.Get());
                }
                CollectDrawn(*child, out);
            }
        }

        /// The calls of the last frame of the elements whose name starts with @p prefix, in the order they were made, one `name@alpha` (or `name(r,g,b,a)`) per call: a rect call belongs to
        /// the element with that rect, the glyphs of a text to the smallest element with a text that holds them (`name:text`, once for a run of glyphs), the tooltip plate is `Plate`.
        /// Calls of no element (the arrow of a dropdown, the glyphs of the tooltip) are left out. A frame with a transition moves the rects: it is read with `CallAt`
        String Trace(StringView prefix, Show show = Show::Alpha) const {
            Vector<const UIElement*> drawn;
            CollectDrawn(_canvas->GetRoot(), drawn);
            String trace;
            String lastText;
            const auto add = [&](const String& name, const Color& color) {
                if (!trace.empty()) {
                    trace += " ";
                }
                trace += show == Show::Alpha ? Format("{}@{}", name, static_cast<int>(color.a)) : Format("{}({})", name, ColorText(color));
            };
            for (const DrawCall& call : _renderer->calls) {
                const UIElement* owner = nullptr;
                String name;
                if (call.kind == DrawKind::Rect) {
                    for (const UIElement* element : drawn) {
                        if (NearRect(element->rect, call.rect)) {
                            owner = element;
                            break;
                        }
                    }
                    if (owner == nullptr && ColorsEqual(call.color, UITooltip::kPlateColor)) {
                        lastText.clear();
                        add("Plate", call.color);
                        continue;
                    }
                    if (owner == nullptr || !StringView(owner->name.ToStringView()).starts_with(prefix)) {
                        continue;
                    }
                    lastText.clear();
                    add(String(owner->name.ToStringView()), call.color);
                    continue;
                }
                for (const UIElement* element : drawn) {
                    if (element->GetComponent<UITextComponent>() != nullptr && RectInside(call.rect, element->rect) &&
                        (owner == nullptr || element->rect.w * element->rect.h < owner->rect.w * owner->rect.h)) {
                        owner = element;
                    }
                }
                if (owner == nullptr || !StringView(owner->name.ToStringView()).starts_with(prefix)) {
                    continue;
                }
                name = Format("{}:text", owner->name.ToStringView());
                if (name != lastText) {
                    lastText = name;
                    add(name, call.color);
                }
            }
            return trace;
        }

        /// The one rect call of the last frame at @p rect, null when there is none
        const DrawCall* CallAt(const Rect& rect) const {
            const DrawCall* found = nullptr;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && NearRect(call.rect, rect)) {
                    EXPECT_EQ(found, nullptr) << "drawn once";
                    found = &call;
                }
            }
            return found;
        }

        Optional<uint8_t> AlphaAt(const Rect& rect) const {
            const DrawCall* call = CallAt(rect);
            return call ? Optional<uint8_t>(call->color.a) : std::nullopt;
        }

        static Vec2 Centre(const UIElement& element) {
            return CentreOf(element.rect);
        }

        static void MoveTo(Vec2 at) {
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(at.x, at.y));
        }

        void Hover(const UIElement& element) {
            MoveTo(Centre(element));
            Draw();
        }

        void Hold(const UIElement& element) {
            const Vec2 at = Centre(element);
            MoveTo(at);
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(at.x, at.y, true));
            Draw();
        }

        /// The pointer away from every element, no button held
        void Release() {
            CoreManager::GetUIInput().DispatchMouseButtonUp(Button(790.0f, 590.0f, false));
            MoveTo({790.0f, 590.0f});
            Draw();
        }
    };

    TEST_F(UICanvasDrawOrderTest, ANestedOpacityMultipliesAndIsGivenBackAfterEachSubtree) {
        // N2 and N3 sit under the 0.5 of N1; N4 has opacity 1 and follows N2, so it shows only the parent's 0.5; N5 follows the whole subtree
        EXPECT_STREQ(Trace("N").c_str(), "N1@100 N2@50 N3@25 N4@100 N5@200");
        EXPECT_EQ(_renderer->GetOpacity(), 1.0f);
    }

    TEST_F(UICanvasDrawOrderTest, AHiddenElementAndItsSubtreeMakeNoCalls) {
        // H1 and its child H2 are hidden; H4 is a hidden child of a shown H3 and does not hide its sibling H5 after it
        EXPECT_STREQ(Trace("H").c_str(), "H3@200 H5@200");
    }

    TEST_F(UICanvasDrawOrderTest, AParentIsDrawnBeforeItsComponentsAndItsComponentsBeforeItsChildren) {
        EXPECT_STREQ(Trace("P").c_str(), "P1@200 P1:text@200 P2@200 P3@200 P3:text@200 P4@200");
    }

    TEST_F(UICanvasDrawOrderTest, ATransitionComposesTheViewAndTheOpacityForTheSubtreeAndGivesThemBackForTheNextSibling) {
        const Rect t1 = El("T1").rect;
        const Rect t2 = El("T2").rect;
        const Rect t3 = El("T3").rect;
        const Rect t4 = El("T4").rect;
        Hide("T1");
        Draw();
        Wait(0.5);

        const Vec2 pivot = CentreOf(t1);
        const Vec2 slide{0.0f, kSlideAtHalf};
        EXPECT_EQ(AlphaAt(Transitioned(t1, pivot, kScaleAtHalf, slide)), kHalf) << "the fade is the progress";
        EXPECT_EQ(AlphaAt(Transitioned(t2, pivot, kScaleAtHalf, slide)), 100) << "the children are moved and faded with it";
        EXPECT_EQ(AlphaAt(Transitioned(t3, pivot, kScaleAtHalf, slide)), 100) << "and the second child too: the view is the same for the whole subtree";
        EXPECT_EQ(AlphaAt(t4), 200) << "the sibling after it is drawn in the view and the opacity from before";
        EXPECT_EQ(_renderer->GetOpacity(), 1.0f);
        EXPECT_EQ(_renderer->GetViewTransform().scale, 1.0f);
        EXPECT_EQ(CallAt(t1), nullptr) << "the laid-out rect is not drawn: the transition moved it";
    }

    TEST_F(UICanvasDrawOrderTest, ATransitionMultipliesWithTheOpacityOfItsOwnElement) {
        const Rect t5 = El("T5").rect;
        Hide("T5");
        Draw();
        Wait(0.5);
        EXPECT_EQ(AlphaAt(t5), kQuarter) << "its own 0.5 and the 0.5 of the progress, in a view that is not scaled";
    }

    TEST_F(UICanvasDrawOrderTest, ACanvasOpacityMultipliesWithEveryElementAndTheTooltipPlateStaysOutsideIt) {
        _canvas->_opacity = 0.5f;
        Draw();
        EXPECT_EQ(AlphaAt(El("N1").rect), kQuarter) << "the product of the canvas and the nesting";
        EXPECT_EQ(AlphaAt(El("N2").rect), kEighth);
        EXPECT_EQ(AlphaAt(El("N5").rect), kHalf);
        EXPECT_STREQ(Trace("T").c_str(), "T1@100 T2@100 T3@100 T4@100 T5@50");
        EXPECT_EQ(_renderer->GetOpacity(), 1.0f);

        Hover(El("Q1"));
        Wait(UITooltip::kHoverDelay + kStep);
        const String trace = Trace("Q");
        EXPECT_TRUE(EndsWith(trace, "Plate@235")) << trace.c_str() << ": the plate comes last, in its own alpha";
    }

    TEST_F(UICanvasDrawOrderTest, ACanvasTransitionAndAnElementTransitionCompose) {
        _canvas->_opacity = 0.5f;
        _canvas->_transitionDuration = 1.0f;
        _canvas->_transitionEase = EaseType::Linear;
        _canvas->_transitionScaleFrom = 0.5f;
        _canvas->_transitionSlideY = 32.0f;
        const Rect t1 = El("T1").rect;
        const Rect t4 = El("T4").rect;
        Hide("T1");
        ASSERT_TRUE(UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("Main"_intern), "Hud"_intern, false));
        Draw();
        Wait(0.5);

        const Vec2 elementPivot = CentreOf(t1);
        const Vec2 canvasPivot{kSceneOutputWidth * 0.5f, kSceneOutputHeight * 0.5f};
        const Vec2 slide{0.0f, kSlideAtHalf};
        const auto moved = [&](const Rect& rect, bool byElement) {
            const Rect inner = byElement ? Transitioned(rect, elementPivot, kScaleAtHalf, slide) : rect;
            return Transitioned(inner, canvasPivot, kScaleAtHalf, slide);
        };
        // The canvas 0.5, its fade 0.5 and, for T1, the element's fade 0.5
        EXPECT_EQ(AlphaAt(moved(t4, false)), kQuarter);
        EXPECT_EQ(AlphaAt(moved(t1, true)), kEighth);
        EXPECT_EQ(AlphaAt(moved(El("T2").rect, true)), kEighth);
        EXPECT_EQ(_renderer->GetOpacity(), 1.0f);
        EXPECT_EQ(_renderer->GetViewTransform().scale, 1.0f) << "the scene's view is given back";
    }

    TEST_F(UICanvasDrawOrderTest, ChildrenOfAClickerAreDrawnInItsStateAndAChildClickerKeepsItsOwn) {
        EXPECT_STREQ(Trace("K", Show::Colour).c_str(), "K1(40,40,40,255) K2:text(200,100,50,255) K3(51,52,53,255) K4:text(111,112,113,255)");

        // The pointer is over K1 (K2 takes no input, so it is K1 that is hovered): K2 follows it, K3 and K4 stay at rest
        Hover(El("K2"));
        EXPECT_STREQ(Trace("K", Show::Colour).c_str(), "K1(60,70,80,255) K2:text(10,20,30,255) K3(51,52,53,255) K4:text(111,112,113,255)");

        Hold(El("K2"));
        EXPECT_STREQ(Trace("K", Show::Colour).c_str(), "K1(20,30,40,255) K2:text(40,50,60,255) K3(51,52,53,255) K4:text(111,112,113,255)");

        // The pointer is over K3: its own state goes to K4, and K1 is not hovered, so K2 is at rest
        Release();
        Hover(El("K3"));
        EXPECT_STREQ(Trace("K", Show::Colour).c_str(), "K1(40,40,40,255) K2:text(200,100,50,255) K3(71,72,73,255) K4:text(121,122,123,255)");

        Hold(El("K3"));
        EXPECT_STREQ(Trace("K", Show::Colour).c_str(), "K1(40,40,40,255) K2:text(200,100,50,255) K3(31,32,33,255) K4:text(131,132,133,255)");
    }

    TEST_F(UICanvasDrawOrderTest, ADisabledClickerShowsItsChildrenDisabledAndANestedOneIsNotAffected) {
        Release();
        ComponentOf<UIInputClicker>("K1").disabled = true;
        Draw();
        EXPECT_STREQ(Trace("K", Show::Colour).c_str(), "K1(90,91,92,255) K2:text(70,80,90,255) K3(51,52,53,255) K4:text(111,112,113,255)");

        ComponentOf<UIInputClicker>("K3").disabled = true;
        Draw();
        EXPECT_STREQ(Trace("K", Show::Colour).c_str(), "K1(90,91,92,255) K2:text(70,80,90,255) K3(81,82,83,255) K4:text(141,142,143,255)");
    }

    TEST_F(UICanvasDrawOrderTest, AnOpenListIsDrawnAfterTheWholeTreeWithTheCanvasOpacityAndNotTheAncestors) {
        _canvas->_opacity = 0.75f;
        Draw();
        EXPECT_STREQ(Trace("D").c_str(), "DGrp@75 DDrop@75 DLater@150") << "closed: the options make no call";

        Get<UIDropdown>("DDrop").Open();
        Draw();
        // DGrp holds the dropdown with its 0.5, the list is not inside that fade: it has the canvas's 0.75 alone, and comes after DLater, which is later in the tree
        EXPECT_STREQ(Trace("D").c_str(), "DGrp@75 DDrop@75 DLater@150 DA@150 DB@150");

        Get<UIDropdown>("DDrop").Close();
        Draw();
        EXPECT_STREQ(Trace("D").c_str(), "DGrp@75 DDrop@75 DLater@150");
    }

    TEST_F(UICanvasDrawOrderTest, TheTooltipPlateIsTheLastDrawOverTheOpenList) {
        Get<UIDropdown>("DDrop").Open();
        Draw();
        Hover(El("Q1"));
        Wait(UITooltip::kHoverDelay + kStep);
        const String trace = Trace("");
        EXPECT_TRUE(EndsWith(trace, "DA@200 DB@200 Plate@235")) << trace.c_str();
    }

    TEST_F(UICanvasDrawOrderTest, ASwitchedThemeIsDrawnAtOnceAndAResetElementTakesItsNewToken) {
        EXPECT_STREQ(Trace("S", Show::Colour).c_str(), "S1(240,240,230,255) S2(240,240,230,255)");

        ASSERT_TRUE(CoreManager::GetUIThemes().SetTheme("dark"_intern));
        Draw();
        EXPECT_STREQ(Trace("S", Show::Colour).c_str(), "S1(30,32,40,255) S2(30,32,40,255)");

        ASSERT_TRUE(CoreManager::GetUIThemes().SetTheme("light"_intern));
        UIElement& s2 = El("S2");
        s2.color = UIColor::FromToken("line"_intern);
        s2.ResetTheme();
        Draw();
        EXPECT_STREQ(Trace("S", Show::Colour).c_str(), "S1(240,240,230,255) S2(10,20,30,255)");
    }

}  // namespace BECore::Tests
