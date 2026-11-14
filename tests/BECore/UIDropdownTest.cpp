#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIChoice.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UITextComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UIPartShapeChecks.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // A Screen canvas in window pixels (output 800 x 600, scale 1). Every header is 240 x 56:
        //   Level (20, 20) bound to `level`, option height 56 and Hard of its own 80; options are {#lvl.*} words
        //   Plain (300, 20) no optionHeight (the header's 56), plain texts
        //   Low   (20, 520) near the bottom edge: its list goes above
        //   Off   (300, 200) disabled
        //   Silent (300, 300) no action
        //   Mixed (500, 400) holds a text with no clicker between its options
        //   Comp  (520, 200) bound to `comp`, composed options (an element with a clicker): a caption on the element, an icon-left one (an image child and a caption child), a disabled one
        //   Both  (520, 450) bound to `both`: a caption option and a second one in one list (it opens above, the list does not fit below)
        constexpr const char* kDropdownXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIDropdown" name="Level" bind="level" action="DdTestLevel" x="20" y="20" width="240" height="56" optionHeight="56" color="40,40,70,255">
            <components><component type="UITextComponent" text="Pick one" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/></components>
            <children>
              <element type="UIElement" name="Easy" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="{#lvl.easy}" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="easy"/>
                </components>
              </element>
              <element type="UIElement" name="Medium" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="{#lvl.medium}" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="medium"/>
                </components>
              </element>
              <element type="UIElement" name="Hard" height="80" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="{#lvl.hard}" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="hard"/>
                </components>
              </element>
              <element type="UIElement" name="Locked" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Locked" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="locked" disabled="true"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIDropdown" name="Plain" bind="plain" action="DdTestPlain" x="300" y="20" width="160" height="56" color="40,40,70,255">
            <components><component type="UITextComponent" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/></components>
            <children>
              <element type="UIElement" name="PlainA" width="260">
                <components>
                  <component type="UITextComponent" text="A" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="a"/>
                </components>
              </element>
              <element type="UIElement" name="PlainB">
                <components>
                  <component type="UITextComponent" text="B" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="b"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIDropdown" name="Low" bind="low" action="DdTestLow" x="20" y="520" width="240" height="56" optionHeight="56" color="40,40,70,255">
            <components><component type="UITextComponent" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/></components>
            <children>
              <element type="UIElement" name="LowA">
                <components>
                  <component type="UITextComponent" text="A" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="a"/>
                </components>
              </element>
              <element type="UIElement" name="LowB">
                <components>
                  <component type="UITextComponent" text="B" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="b"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIDropdown" name="Off" bind="off" action="DdTestOff" x="300" y="200" width="240" height="56" optionHeight="56" disabled="true">
            <components><component type="UITextComponent" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/></components>
            <children>
              <element type="UIElement" name="OffA">
                <components>
                  <component type="UITextComponent" text="A" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="a"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIDropdown" name="Silent" bind="silent" x="300" y="300" width="240" height="56" optionHeight="56">
            <components><component type="UITextComponent" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/></components>
            <children>
              <element type="UIElement" name="SilentA">
                <components>
                  <component type="UITextComponent" text="A" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="a"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIDropdown" name="Mixed" bind="mixed" action="DdTestMixed" x="500" y="400" width="240" height="56" optionHeight="56">
            <components><component type="UITextComponent" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/></components>
            <children>
              <element type="UIElement" name="Caption">
                <components><component type="UITextComponent" text="Caption"/></components>
              </element>
              <element type="UIElement" name="MixedA">
                <components>
                  <component type="UITextComponent" text="A" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="a"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIDropdown" name="Comp" bind="comp" action="DdTestComp" x="520" y="200" width="240" height="56" optionHeight="56" color="40,40,70,255">
            <components><component type="UITextComponent" text="Pick" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/></components>
            <children>
              <element type="UIElement" name="CompEasy" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="{#lvl.easy}" align="Center" verticalAlign="Center" textColor="10,200,30,255"/>
                  <component type="UIInputClicker" value="easy"/>
                </components>
              </element>
              <element type="UIElement" name="CompHard" color="70,70,100,255" layout="Stack" direction="Horizontal" spacing="6" padding="8,0,0,0" crossAlign="Center">
                <components><component type="UIInputClicker" value="hard"/></components>
                <children>
                  <element type="UIElement" name="CompHardIcon" width="24" height="24"><components><component type="UIImageComponent"/></components></element>
                  <element type="UIElement" name="CompHardCaption" flex="1" height="100%">
                    <components><component type="UITextComponent" text="{#lvl.hard}" align="Center" verticalAlign="Center"/></components>
                  </element>
                </children>
              </element>
              <element type="UIElement" name="CompLocked" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Locked" align="Center" verticalAlign="Center"/>
                  <component type="UIInputClicker" value="locked" disabled="true"/>
                </components>
              </element>
            </children>
          </element>
          <element type="UIDropdown" name="Both" bind="both" action="DdTestBoth" x="520" y="450" width="240" height="56" optionHeight="56" color="40,40,70,255">
            <components><component type="UITextComponent" text="Pick" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/></components>
            <children>
              <element type="UIElement" name="BothButton" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="{#lvl.medium}" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" value="button"/>
                </components>
              </element>
              <element type="UIElement" name="BothComposed" color="70,70,100,255">
                <components>
                  <component type="UITextComponent" text="Composed" align="Center" verticalAlign="Center"/>
                  <component type="UIInputClicker" value="composed"/>
                </components>
              </element>
            </children>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr const char* kStringsXml = R"(
<UIStrings default="en">
  <language id="en">
    <string key="lvl.easy" text="Easy"/>
    <string key="lvl.medium" text="Medium"/>
    <string key="lvl.hard" text="Hard"/>
  </language>
  <language id="ru">
    <string key="lvl.easy" text="Легко"/>
    <string key="lvl.medium" text="Средне"/>
    <string key="lvl.hard" text="Сложно"/>
  </language>
</UIStrings>)";

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        /// The actions the buttons emit, with the value each carried, in order
        class ActionLog : public SubscriptionHolder {
        public:
            ActionLog() {
                Subscribe<InputEvents::ActionEvent, &ActionLog::OnAction>(this);
            }

            size_t Total() const {
                return _actions.size();
            }
            PoolString LastAction() const {
                return _actions.empty() ? PoolString{} : _actions.back();
            }
            PoolString LastValue() const {
                return _values.empty() ? PoolString{} : _values.back();
            }

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                _actions.push_back(event.action);
                _values.push_back(event.value);
            }

            Vector<PoolString> _actions;
            Vector<PoolString> _values;
        };

        void SetVar(const char* name, const char* value) {
            CoreManager::GetUIVars().Set(PoolString::Intern(name), value);
        }

        /// The header text as shown: the chosen option's caption, else the placeholder of the text component
        StringView HeaderText(const UIDropdown& dropdown) {
            const UITextComponent* header = dropdown.GetComponent<UITextComponent>();
            return header ? header->GetDisplayText(dropdown) : StringView{};
        }

        bool SameRect(const Rect& a, const Rect& b) {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

    }  // namespace

    class UIDropdownTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetFontAtlasLibrary().Clear();
            UIStrings& strings = CoreManager::GetUIStrings();
            strings.Clear();
            auto config = XmlConfig::Create();
            ASSERT_TRUE(config.LoadFromString(kStringsXml));
            ASSERT_TRUE(strings.LoadFromXml(config.GetRoot()).empty());

            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kDropdownXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();

            _canvas = _manager->GetSceneByName("Main"_intern)->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);
            Draw();
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();
            _canvas.Reset();
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        void Draw() {
            _manager->SetActiveScene("Main"_intern);
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        template <typename T>
        T& Get(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            T* typed = element ? element->Cast<T>() : nullptr;
            EXPECT_NE(typed, nullptr) << name << " is not the expected control";
            return *typed;
        }

        /// Any element by name, whatever its class (`Get<T>` wants the exact type)
        UIElement& Element(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(element, nullptr) << name;
            return *element;
        }

        /// The textured quads (the glyphs of a caption) drawn inside @p area in the last draw
        size_t GlyphQuadsIn(const Rect& area) const {
            size_t count = 0;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.rect.x >= area.x - 1e-2f && call.rect.y >= area.y - 1e-2f && call.rect.x + call.rect.w <= area.x + area.w + 1e-2f &&
                    call.rect.y + call.rect.h <= area.y + area.h + 1e-2f) {
                    ++count;
                }
            }
            return count;
        }

        /// A press and a release at the middle of the element, by the layout of the last draw; false when either was not consumed
        bool Click(const char* name) {
            const UIElement* found = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            EXPECT_NE(found, nullptr) << name;
            if (!found) {
                return false;
            }
            const UIElement& element = *found;
            const float x = element.rect.x + element.rect.w * 0.5f;
            const float y = element.rect.y + element.rect.h * 0.5f;
            return ClickAt(x, y);
        }

        static bool ClickAt(float x, float y) {
            const bool down = CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));
            const bool up = CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
            return down && up;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
    };

    // ---- the header -------------------------------------------------------------------------------------------------------

    TEST_F(UIDropdownTest, ADropdownFromAFileIsAnInteractiveElementWithItsOptionsAsChildren) {
        const auto& level = Get<UIDropdown>("Level");
        static_assert(std::is_base_of_v<UIElement, UIDropdown>);
        ASSERT_NE(level.GetComponent<UITextComponent>(), nullptr) << "the header text is a component";
        EXPECT_TRUE(level.interactive);
        EXPECT_EQ(level.bind, "level"_intern);
        EXPECT_EQ(level.action, "DdTestLevel"_intern);
        EXPECT_FLOAT_EQ(level.optionHeight, 56.0f);
        EXPECT_FALSE(level.disabled);
        EXPECT_FALSE(level.IsOpen());
        EXPECT_EQ(level.children.size(), 4u);
        EXPECT_TRUE(Get<UIDropdown>("Off").disabled);
    }

    TEST_F(UIDropdownTest, AnUnsetVariableShowsThePlaceholder) {
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Level")), "Pick one");
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Plain")), "");

        SetVar("level", "nothing like it");
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Level")), "Pick one") << "a value no option has chosen nothing";
    }

    TEST_F(UIDropdownTest, TheHeaderShowsTheTextOfTheChosenOption) {
        SetVar("level", "medium");
        Draw();

        EXPECT_EQ(HeaderText(Get<UIDropdown>("Level")), "Medium");
        EXPECT_EQ(Get<UIDropdown>("Level").FindChosenOption(), &Element("Medium"));
        EXPECT_TRUE(Element("Medium").selected);
        EXPECT_FALSE(Element("Easy").selected);

        SetVar("plain", "b");
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Plain")), "B");
    }

    TEST_F(UIDropdownTest, ALanguageSwitchRewordsTheHeader) {
        SetVar("level", "hard");
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Level")), "Hard");

        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("ru"_intern));
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Level")), "Сложно");
    }

    TEST_F(UIDropdownTest, TheShownTextIsNeverWrittenIntoTheAuthoredText) {
        SetVar("level", "easy");
        Draw();
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Level")), "Easy");
        EXPECT_EQ(Get<UIDropdown>("Level").GetComponent<UITextComponent>()->text, "Pick one");

        CoreManager::GetUIVars().Remove("level"_intern);
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Level")), "Pick one");
    }

    TEST_F(UIDropdownTest, TheHeaderTextIsLaidOutLeftOfTheArrow) {
        const Rect header{0.0f, 0.0f, 240.0f, 56.0f};
        const auto parts = UIDropdown::GetHeaderParts(header);
        EXPECT_GE(parts.text.x, header.x);
        EXPECT_LE(parts.text.x + parts.text.w, parts.arrow.x);
        EXPECT_LE(parts.arrow.x + parts.arrow.w, header.x + header.w);
        EXPECT_GE(parts.arrow.y, header.y);
        EXPECT_LE(parts.arrow.y + parts.arrow.h, header.y + header.h);

        const auto& level = Get<UIDropdown>("Level");
        EXPECT_TRUE(level.CheckContent(240.0f, 56.0f, 1.0f).fits);
        // Narrow enough that the arrow leaves no room for the text
        const auto narrow = level.CheckContent(30.0f, 56.0f, 1.0f);
        EXPECT_TRUE(narrow.hasContent);
        EXPECT_FALSE(narrow.fits);
    }

    TEST_F(UIDropdownTest, TheHeaderTextComponentDrawsInTheTextAreaAndShowsTheChosenCaption) {
        auto& level = Get<UIDropdown>("Level");
        SetVar("level", "medium");
        Draw();

        const Rect text = UIDropdown::GetHeaderParts(level.rect).text;
        EXPECT_TRUE(SameRect(level.GetComponentArea(), text)) << "the component draws left of the arrow, not in the whole rect";
        ASSERT_NE(level.GetTextOverride(), nullptr);
        EXPECT_EQ(*level.GetTextOverride(), UIChoice::GetText(*level.FindChosenOption())) << "the caption of the chosen option, not expanded";
        const size_t glyphs = GlyphQuadsIn(text);
        EXPECT_EQ(glyphs, 6u) << "Medium: six glyphs, all inside the text area";

        CoreManager::GetUIVars().Remove("level"_intern);
        EXPECT_EQ(level.GetTextOverride(), nullptr) << "nothing chosen: the component shows its own text, the placeholder";
        Draw();
        EXPECT_EQ(GlyphQuadsIn(text), 7u) << "Pick one: seven glyphs and a space";
    }

    TEST_F(UIDropdownTest, ADisabledHeaderFadesItsText) {
        auto& level = Get<UIDropdown>("Level");
        const Rect text = UIDropdown::GetHeaderParts(level.rect).text;
        const auto lastGlyphAlpha = [&]() {
            int alpha = -1;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.rect.x >= text.x && call.rect.x + call.rect.w <= text.x + text.w) {
                    alpha = call.color.a;
                }
            }
            return alpha;
        };
        Draw();
        EXPECT_EQ(lastGlyphAlpha(), 255);

        level.disabled = true;
        Draw();
        EXPECT_EQ(lastGlyphAlpha(), WithAlpha(Color{255, 255, 255, 255}, kDisabledAlpha).a) << "fadeWhenDisabled of the header component";
    }

    TEST_F(UIDropdownTest, ADropdownWithoutATextComponentHasNoContentToFit) {
        UIDropdown bare;
        EXPECT_TRUE(bare.CheckContent(30.0f, 56.0f, 1.0f).fits) << "a header with no text component has no content to fit";
        EXPECT_FALSE(bare.CheckContent(30.0f, 56.0f, 1.0f).hasContent);
    }

    TEST_F(UIDropdownTest, TheArrowPointsDownClosedAndUpOpen) {
        const Rect box{0.0f, 0.0f, 16.0f, 12.0f};
        const auto down = UIDropdown::GetArrowStrips(box, false);
        const auto up = UIDropdown::GetArrowStrips(box, true);
        EXPECT_GT(down.front().w, down.back().w);
        EXPECT_LT(up.front().w, up.back().w);
        EXPECT_FLOAT_EQ(down.front().w, box.w);
        EXPECT_FLOAT_EQ(down.front().y, box.y);
        EXPECT_FLOAT_EQ(down.back().y + down.back().h, box.y + box.h);

        // The widest strip of the drawn arrow is at the top while closed and at the bottom while open
        const auto widestRowOf = [&](const Rect& arrowBox) {
            float widest = 0.0f;
            float widestY = 0.0f;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && call.rect.x >= arrowBox.x - 0.01f && call.rect.x + call.rect.w <= arrowBox.x + arrowBox.w + 0.01f && call.rect.y >= arrowBox.y - 0.01f &&
                    call.rect.y + call.rect.h <= arrowBox.y + arrowBox.h + 0.01f && call.rect.w > widest) {
                    widest = call.rect.w;
                    widestY = call.rect.y;
                }
            }
            return widestY;
        };
        auto& level = Get<UIDropdown>("Level");
        const Rect arrow = UIDropdown::GetHeaderParts(level.rect).arrow;
        EXPECT_NEAR(widestRowOf(arrow), arrow.y, 0.01f);

        level.Open();
        Draw();
        EXPECT_GT(widestRowOf(arrow), arrow.y + arrow.h * 0.5f);
    }

    TEST_F(UIDropdownTest, ADisabledDropdownIsDrawnGrey) {
        auto& off = Get<UIDropdown>("Off");
        auto& level = Get<UIDropdown>("Level");
        EXPECT_LT(off.GetCurrentArrow().a, level.GetCurrentArrow().a);
        EXPECT_LT(off.GetCurrentFill().a, 255);  // transparent `color` stays transparent, a colored one loses half its alpha
        EXPECT_EQ(level.GetCurrentFill().a, 255);
        level.color = Color{40, 40, 70, 255};
        level.disabled = true;
        EXPECT_EQ(level.GetCurrentFill().a, static_cast<uint8_t>(255 * kDisabledAlpha));
        level.arrowColor = Color{1, 2, 3, 200};
        EXPECT_EQ(level.GetCurrentArrow().r, 1);
    }

    // ---- the pure geometry ------------------------------------------------------------------------------------------------

    TEST(UIDropdownGeometryTest, AListThatFitsBelowIsUnderTheHeaderAsWideAsIt) {
        const Rect bounds{0.0f, 0.0f, 800.0f, 600.0f};
        const auto geometry = UIDropdown::GetPopupGeometry({20.0f, 20.0f, 240.0f, 56.0f}, {{0.0f, 56.0f}, {0.0f, 40.0f}}, bounds);

        EXPECT_FALSE(geometry.above);
        ASSERT_EQ(geometry.options.size(), 2u);
        EXPECT_TRUE(SameRect(geometry.options[0], {20.0f, 76.0f, 240.0f, 56.0f}));
        EXPECT_TRUE(SameRect(geometry.options[1], {20.0f, 132.0f, 240.0f, 40.0f}));
        EXPECT_TRUE(SameRect(geometry.list, {20.0f, 76.0f, 240.0f, 96.0f}));
    }

    TEST(UIDropdownGeometryTest, AListThatDoesNotFitBelowButFitsAboveFlips) {
        const Rect bounds{0.0f, 0.0f, 800.0f, 600.0f};
        const auto geometry = UIDropdown::GetPopupGeometry({20.0f, 520.0f, 240.0f, 56.0f}, {{0.0f, 56.0f}, {0.0f, 56.0f}}, bounds);

        EXPECT_TRUE(geometry.above);
        EXPECT_TRUE(SameRect(geometry.options[0], {20.0f, 408.0f, 240.0f, 56.0f}));
        EXPECT_TRUE(SameRect(geometry.options[1], {20.0f, 464.0f, 240.0f, 56.0f}));
        EXPECT_FLOAT_EQ(geometry.list.y + geometry.list.h, 520.0f) << "directly over the header";
    }

    TEST(UIDropdownGeometryTest, AListThatFitsNeitherSideIsShiftedIntoTheBounds) {
        const Rect bounds{0.0f, 0.0f, 800.0f, 300.0f};
        // 5 x 56 = 280 high; under the header at y = 100 it would end at 436, over it would start at -180
        const auto geometry = UIDropdown::GetPopupGeometry({20.0f, 100.0f, 240.0f, 56.0f}, {{0.0f, 56.0f}, {0.0f, 56.0f}, {0.0f, 56.0f}, {0.0f, 56.0f}, {0.0f, 56.0f}}, bounds);

        EXPECT_FALSE(geometry.above);
        EXPECT_FLOAT_EQ(geometry.list.y, 20.0f);
        EXPECT_FLOAT_EQ(geometry.list.y + geometry.list.h, 300.0f);
    }

    TEST(UIDropdownGeometryTest, AListTallerThanTheBoundsStartsAtTheirTop) {
        const Rect bounds{0.0f, 40.0f, 800.0f, 100.0f};
        const auto geometry = UIDropdown::GetPopupGeometry({20.0f, 60.0f, 240.0f, 56.0f}, {{0.0f, 80.0f}, {0.0f, 80.0f}}, bounds);

        EXPECT_FLOAT_EQ(geometry.list.y, 40.0f);
        EXPECT_GT(geometry.list.y + geometry.list.h, bounds.y + bounds.h) << "the validator reports it, nothing scrolls";
    }

    TEST(UIDropdownGeometryTest, AColumnThatWouldLeaveOnTheRightIsShiftedLeft) {
        const Rect bounds{0.0f, 0.0f, 400.0f, 600.0f};
        const auto geometry = UIDropdown::GetPopupGeometry({300.0f, 20.0f, 100.0f, 56.0f}, {{200.0f, 56.0f}}, bounds);

        EXPECT_FLOAT_EQ(geometry.list.w, 200.0f) << "an option asks for more than the header";
        EXPECT_FLOAT_EQ(geometry.list.x, 200.0f);
        EXPECT_FLOAT_EQ(geometry.list.x + geometry.list.w, 400.0f);
    }

    TEST(UIDropdownGeometryTest, TheColumnStaysInsideBoundsEvenWhenTheHeaderLeavesThemOnTheLeft) {
        const Rect bounds{50.0f, 0.0f, 300.0f, 600.0f};
        const auto geometry = UIDropdown::GetPopupGeometry({10.0f, 20.0f, 100.0f, 56.0f}, {{0.0f, 56.0f}}, bounds);
        EXPECT_FLOAT_EQ(geometry.list.x, 50.0f);

        const auto wide = UIDropdown::GetPopupGeometry({60.0f, 20.0f, 100.0f, 56.0f}, {{900.0f, 56.0f}}, bounds);
        EXPECT_FLOAT_EQ(wide.list.w, 300.0f);
        EXPECT_FLOAT_EQ(wide.list.x, 50.0f);
    }

    TEST(UIDropdownGeometryTest, NoOptionsIsAnEmptyList) {
        const auto geometry = UIDropdown::GetPopupGeometry({20.0f, 20.0f, 240.0f, 56.0f}, {}, {0.0f, 0.0f, 800.0f, 600.0f});
        EXPECT_TRUE(geometry.options.empty());
        EXPECT_FLOAT_EQ(geometry.list.h, 0.0f);
    }

    // ---- the list in a canvas ---------------------------------------------------------------------------------------------

    TEST_F(UIDropdownTest, ClosedTheOptionsHaveNoRect) {
        for (const char* name : {"Easy", "Medium", "Hard", "Locked"}) {
            EXPECT_TRUE(Element(name).rect.IsEmpty()) << name;
        }
        EXPECT_TRUE(_canvas->GetLayout().OpenPopups().empty());
    }

    TEST_F(UIDropdownTest, OpenTheOptionHeightComesFromTheOptionThenTheDropdownThenTheHeader) {
        Get<UIDropdown>("Plain").Open();
        Draw();
        // No optionHeight, so the header's 56; PlainA asks for 260 units of width, which makes the column that wide
        EXPECT_FLOAT_EQ(Element("PlainA").rect.h, 56.0f);
        EXPECT_FLOAT_EQ(Element("PlainB").rect.h, 56.0f);
        EXPECT_FLOAT_EQ(Element("PlainA").rect.w, 260.0f);
        EXPECT_FLOAT_EQ(Element("PlainB").rect.w, 260.0f) << "one column";
        EXPECT_FLOAT_EQ(Element("PlainA").rect.y, 76.0f);
        EXPECT_FLOAT_EQ(Element("PlainB").rect.y, 132.0f);

        Get<UIDropdown>("Level").Open();  // the newest popup wins, Plain is closed
        Draw();
        EXPECT_FALSE(Get<UIDropdown>("Plain").IsOpen());
        EXPECT_FLOAT_EQ(Element("Easy").rect.h, 56.0f) << "optionHeight of the dropdown";
        EXPECT_FLOAT_EQ(Element("Hard").rect.h, 80.0f) << "the option's own height wins";
        EXPECT_FLOAT_EQ(Element("Easy").rect.y, 76.0f);
        EXPECT_FLOAT_EQ(Element("Medium").rect.y, 132.0f);
        EXPECT_FLOAT_EQ(Element("Hard").rect.y, 188.0f);
        EXPECT_FLOAT_EQ(Element("Locked").rect.y, 268.0f);
        EXPECT_FLOAT_EQ(Element("Easy").rect.w, 240.0f);
    }

    TEST_F(UIDropdownTest, AListNearTheBottomEdgeOpensAboveTheHeader) {
        Get<UIDropdown>("Low").Open();
        Draw();

        EXPECT_FLOAT_EQ(Element("LowA").rect.y, 408.0f);
        EXPECT_FLOAT_EQ(Element("LowB").rect.y, 464.0f);
    }

    TEST_F(UIDropdownTest, OnlyTheButtonsAreOptions) {
        auto& mixed = Get<UIDropdown>("Mixed");
        mixed.Open();
        SetVar("mixed", "x");  // the text has no clicker, so it is no option
        Draw();

        EXPECT_EQ(mixed.FindChosenOption(), nullptr);
        EXPECT_TRUE(Element("Caption").rect.IsEmpty());
        EXPECT_FLOAT_EQ(Element("MixedA").rect.y, 456.0f) << "the text takes no room in the list";
    }

    // ---- input ------------------------------------------------------------------------------------------------------------

    TEST_F(UIDropdownTest, AClickOnTheHeaderOpensTheListAndAnotherClosesIt) {
        ActionLog log;
        EXPECT_TRUE(Click("Level"));
        EXPECT_TRUE(Get<UIDropdown>("Level").IsOpen());
        Draw();
        EXPECT_FALSE(Element("Easy").rect.IsEmpty());

        EXPECT_TRUE(Click("Level"));
        EXPECT_FALSE(Get<UIDropdown>("Level").IsOpen());
        Draw();
        EXPECT_TRUE(Element("Easy").rect.IsEmpty());
        EXPECT_EQ(log.Total(), 0u) << "opening and closing emit nothing";
    }

    TEST_F(UIDropdownTest, AClickOnAnOptionEmitsTheActionWithItsValueAndClosesTheList) {
        ActionLog log;
        Get<UIDropdown>("Level").Open();
        Draw();

        EXPECT_TRUE(Click("Medium"));

        ASSERT_EQ(log.Total(), 1u);
        EXPECT_EQ(log.LastAction(), "DdTestLevel"_intern);
        EXPECT_EQ(log.LastValue(), "medium"_intern);
        EXPECT_FALSE(Get<UIDropdown>("Level").IsOpen());
        Draw();
        EXPECT_TRUE(Element("Medium").rect.IsEmpty());
    }

    TEST_F(UIDropdownTest, AChoiceChangesNoVariable) {
        SetVar("level", "easy");
        Get<UIDropdown>("Level").Open();
        Draw();
        ASSERT_TRUE(Click("Hard"));

        EXPECT_EQ(CoreManager::GetUIVars().Get("level"_intern), "easy") << "the game answers the action, the dropdown does not";
        Draw();
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Level")), "Easy");
    }

    TEST_F(UIDropdownTest, ADisabledDropdownNeverOpens) {
        ActionLog log;
        auto& off = Get<UIDropdown>("Off");
        EXPECT_TRUE(Click("Off")) << "the header still takes the click";
        EXPECT_FALSE(off.IsOpen());
        off.Open();
        EXPECT_FALSE(off.IsOpen());
        off.Toggle();
        EXPECT_FALSE(off.IsOpen());
        EXPECT_EQ(log.Total(), 0u);
    }

    TEST_F(UIDropdownTest, AnOpenDropdownThatGetsDisabledClosesItsList) {
        auto& level = Get<UIDropdown>("Level");
        level.Open();
        Draw();
        ASSERT_FALSE(Element("Easy").rect.IsEmpty());

        level.disabled = true;
        Draw();  // the canvas notices the list is gone and invalidates
        Draw();

        EXPECT_FALSE(level.IsOpen());
        EXPECT_TRUE(Element("Easy").rect.IsEmpty());
    }

    TEST_F(UIDropdownTest, ADisabledOptionTakesTheClickAndTheListStaysOpen) {
        ActionLog log;
        Get<UIDropdown>("Level").Open();
        Draw();

        EXPECT_TRUE(Click("Locked"));

        EXPECT_EQ(log.Total(), 0u);
        EXPECT_TRUE(Get<UIDropdown>("Level").IsOpen());
    }

    TEST_F(UIDropdownTest, ADropdownWithoutAnActionClosesOnAChoiceAndEmitsNothing) {
        ActionLog log;
        Get<UIDropdown>("Silent").Open();
        Draw();

        EXPECT_TRUE(Click("SilentA"));

        EXPECT_EQ(log.Total(), 0u);
        EXPECT_FALSE(Get<UIDropdown>("Silent").IsOpen());
    }

    TEST_F(UIDropdownTest, APressOutsideClosesTheListAndEmitsNothing) {
        ActionLog log;
        Get<UIDropdown>("Level").Open();
        Draw();

        EXPECT_TRUE(ClickAt(700.0f, 550.0f));

        EXPECT_FALSE(Get<UIDropdown>("Level").IsOpen());
        EXPECT_EQ(log.Total(), 0u);
    }

    TEST_F(UIDropdownTest, AClickOnAnOtherDropdownHeaderClosesTheFirstAndDoesNotOpenTheSecond) {
        Get<UIDropdown>("Level").Open();
        Draw();

        EXPECT_TRUE(Click("Plain"));  // an outside press: it only closes
        EXPECT_FALSE(Get<UIDropdown>("Level").IsOpen());
        EXPECT_FALSE(Get<UIDropdown>("Plain").IsOpen());

        EXPECT_TRUE(Click("Plain"));
        EXPECT_TRUE(Get<UIDropdown>("Plain").IsOpen());
    }

    TEST_F(UIDropdownTest, AHiddenDropdownClosesItsList) {
        auto& level = Get<UIDropdown>("Level");
        level.Open();
        Draw();
        ASSERT_TRUE(_canvas->SetElementVisible("Level"_intern, false));

        Draw();
        Draw();

        EXPECT_FALSE(level.IsOpen());
        EXPECT_TRUE(_canvas->GetLayout().OpenPopups().empty());
        ASSERT_TRUE(_canvas->SetElementVisible("Level"_intern, true));
        Draw();
        EXPECT_FALSE(level.IsOpen()) << "shown again, it is closed";
        EXPECT_TRUE(Element("Easy").rect.IsEmpty());
    }

    TEST_F(UIDropdownTest, AnOpenListIsValidatedLikeAnythingElse) {
        EXPECT_TRUE(_canvas->GetLayout().Validate({}).empty());
        Get<UIDropdown>("Level").optionHeight = 20.0f;  // Easy and Medium become 20 high, below a touch target
        Get<UIDropdown>("Level").Open();
        Draw();

        size_t small = 0;
        for (const auto& issue : _canvas->GetLayout().Validate({})) {
            small += issue.kind == UILayoutIssueKind::TouchTargetTooSmall ? 1u : 0u;
        }
        EXPECT_GE(small, 2u);
    }

    // ---- the header is a box --------------------------------------------------------------------------------------------------

    namespace {

        bool SameRgba(const Color& a, const Color& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        }

        bool NearRect(const Rect& a, const Rect& b) {
            return std::abs(a.x - b.x) < 1e-2f && std::abs(a.y - b.y) < 1e-2f && std::abs(a.w - b.w) < 1e-2f && std::abs(a.h - b.h) < 1e-2f;
        }

        enum class HeaderState { Idle, Hover, Press, Disabled };

        void SetState(UIDropdown& dropdown, HeaderState state) {
            dropdown.disabled = state == HeaderState::Disabled;
            dropdown.hovered = state == HeaderState::Hover || state == HeaderState::Press;
            dropdown.pressed = state == HeaderState::Press;
        }

        constexpr HeaderState kStates[] = {HeaderState::Idle, HeaderState::Hover, HeaderState::Press, HeaderState::Disabled};

        Color Shifted(const Color& base, HeaderState state) {
            switch (state) {
                case HeaderState::Hover:
                    return ShiftColor(base, kHoverBrighten);
                case HeaderState::Press:
                    return ShiftColor(base, -kPressDarken);
                case HeaderState::Disabled:
                    return WithAlpha(base, kDisabledAlpha);
                case HeaderState::Idle:
                    break;
            }
            return base;
        }

    }  // namespace

    TEST_F(UIDropdownTest, ADefaultHeaderIsOneRectOfTheStateColorInEveryState) {
        auto& level = Get<UIDropdown>("Level");
        for (const HeaderState state : kStates) {
            SetState(level, state);
            Draw();
            size_t headerRects = 0;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && NearRect(call.rect, level.rect)) {
                    ++headerRects;
                    EXPECT_TRUE(SameRgba(call.color, Shifted(Color{40, 40, 70, 255}, state))) << static_cast<int>(state);
                    EXPECT_TRUE(SameRgba(call.color, level.GetCurrentFill())) << static_cast<int>(state);
                }
            }
            EXPECT_EQ(headerRects, 1u) << static_cast<int>(state);
            EXPECT_TRUE(CallsIn(*_renderer, DrawKind::Geometry, level.rect).empty()) << static_cast<int>(state);
        }
    }

    TEST_F(UIDropdownTest, AStyledHeaderIsOneGeometryCallAndNoHeaderRect) {
        auto& level = Get<UIDropdown>("Level");
        level.box.cornerRadius = UICorners::Uniform(12.0f);
        level.box.borderWidth = 2.0f;
        level.box.borderColor = Color{200, 200, 255, 255};
        Draw();
        for (const DrawCall& call : _renderer->calls) {
            EXPECT_FALSE(call.kind == DrawKind::Rect && NearRect(call.rect, level.rect)) << "the plain rect is gone";
        }
        const auto geometry = CallsIn(*_renderer, DrawKind::Geometry, level.rect);
        ASSERT_EQ(geometry.size(), 1u);
        EXPECT_GE(geometry[0].rect.w, level.rect.w - UIBoxMesh::kFeatherPixels);
        EXPECT_GE(geometry[0].rect.h, level.rect.h - UIBoxMesh::kFeatherPixels);
    }

    TEST_F(UIDropdownTest, TheArrowIsStillDrawnAsStripsUnderAStyledHeader) {
        auto& level = Get<UIDropdown>("Level");
        level.box.cornerRadius = UICorners::Uniform(12.0f);
        level.box.shadow = {0.0f, 4.0f, 6.0f, 0.0f, Color{0, 0, 0, 120}};
        Draw();
        const Rect arrow = UIDropdown::GetHeaderParts(level.rect).arrow;
        EXPECT_EQ(CallsIn(*_renderer, DrawKind::Rect, arrow).size(), static_cast<size_t>(UIDropdown::kArrowRows));
    }

    TEST_F(UIDropdownTest, AGradientHeaderKeepsTwoDistinctEndsInEveryState) {
        auto& level = Get<UIDropdown>("Level");
        level.box.gradientDirection = UIGradientDirection::Vertical;
        level.box.gradientColor = Color{120, 90, 160, 255};
        for (const HeaderState state : kStates) {
            SetState(level, state);
            const UIStateLook::Look look = level.GetCurrentBox();
            EXPECT_TRUE(SameRgba(look.fill, Shifted(Color{40, 40, 70, 255}, state))) << static_cast<int>(state);
            EXPECT_TRUE(SameRgba(look.gradientEnd, Shifted(Color{120, 90, 160, 255}, state))) << static_cast<int>(state);
            EXPECT_FALSE(SameRgba(look.fill, look.gradientEnd)) << static_cast<int>(state);
            EXPECT_TRUE(SameRgba(look.box.gradientColor, look.gradientEnd)) << "the look says the far end twice";
            EXPECT_TRUE(SameRgba(level.GetCurrentFill(), look.fill));
        }

        SetState(level, HeaderState::Hover);
        Draw();
        const auto geometry = CallsIn(*_renderer, DrawKind::Geometry, level.rect);
        ASSERT_EQ(geometry.size(), 1u);
        uint8_t highestRed = 0;
        uint8_t lowestRed = 255;
        for (const GeometryVertex& vertex : geometry[0].vertices) {
            highestRed = std::max(highestRed, vertex.color.r);
            lowestRed = std::min(lowestRed, vertex.color.r);
        }
        EXPECT_EQ(highestRed, Shifted(Color{120, 90, 160, 255}, HeaderState::Hover).r) << "the bottom of the hovered header";
        EXPECT_EQ(lowestRed, Shifted(Color{40, 40, 70, 255}, HeaderState::Hover).r) << "its top";
    }

    TEST_F(UIDropdownTest, ADisabledHeaderDropsShadowAndGlowAndFadesBorderAndHighlight) {
        auto& level = Get<UIDropdown>("Level");
        level.box.cornerRadius = UICorners::Uniform(8.0f);
        level.box.borderWidth = 2.0f;
        level.box.borderColor = Color{200, 100, 50, 200};
        level.box.highlight = Color{255, 255, 255, 100};
        level.box.shadow = {0.0f, 6.0f, 12.0f, 0.0f, Color{0, 0, 0, 160}};
        level.box.glow = {0.0f, 0.0f, 10.0f, 2.0f, Color{255, 200, 0, 128}};

        SetState(level, HeaderState::Disabled);
        const UIStateLook::Look off = level.GetCurrentBox();
        EXPECT_EQ(off.box.shadow.color.a, 0);
        EXPECT_EQ(off.box.glow.color.a, 0);
        EXPECT_EQ(off.box.borderColor.a, WithAlpha(Color{200, 100, 50, 200}, kDisabledAlpha).a);
        EXPECT_EQ(off.box.highlight.a, WithAlpha(Color{255, 255, 255, 100}, kDisabledAlpha).a);
        Draw();
        const auto geometry = CallsIn(*_renderer, DrawKind::Geometry, level.rect);
        ASSERT_EQ(geometry.size(), 1u);
        EXPECT_LE(geometry[0].rect.x + geometry[0].rect.w, level.rect.x + level.rect.w + UIBoxMesh::kFeatherPixels) << "nothing is drawn beyond the header: no shadow, no glow";
        EXPECT_LE(geometry[0].rect.y + geometry[0].rect.h, level.rect.y + level.rect.h + UIBoxMesh::kFeatherPixels);

        for (const HeaderState state : {HeaderState::Idle, HeaderState::Hover, HeaderState::Press}) {
            SetState(level, state);
            const UIBoxStyle& box = level.GetCurrentBox().box;
            EXPECT_TRUE(box.shadow == level.box.shadow) << static_cast<int>(state);
            EXPECT_TRUE(box.glow == level.box.glow) << static_cast<int>(state);
            EXPECT_TRUE(SameRgba(box.borderColor, level.box.borderColor)) << "hover and press keep the border as authored";
            EXPECT_TRUE(SameRgba(box.highlight, level.box.highlight)) << "and the highlight";
        }
    }

    TEST_F(UIDropdownTest, TheArrowColourComesFromArrowColourThenTheTextComponentThenWhite) {
        auto& level = Get<UIDropdown>("Level");
        UITextComponent& header = *level.GetComponent<UITextComponent>();
        header.textColor = UIColor{10, 20, 30, 255};
        EXPECT_TRUE(SameRgba(level.GetCurrentArrow(), Color{10, 20, 30, 255})) << "the text colour of the header component";

        level.arrowColor = Color{1, 2, 3, 200};
        EXPECT_TRUE(SameRgba(level.GetCurrentArrow(), Color{1, 2, 3, 200})) << "arrowColor wins";

        UIDropdown bare;
        EXPECT_TRUE(SameRgba(bare.GetCurrentArrow(), Color{255, 255, 255, 255})) << "no header component: white";
        EXPECT_EQ(bare.GetTextOverride(), nullptr);
    }

    TEST_F(UIDropdownTest, TheOptionsKeepTheBoxOfTheirOwn) {
        auto& easy = Element("Easy");
        easy.box.cornerRadius = UICorners::Uniform(10.0f);
        Get<UIDropdown>("Level").Open();
        Draw();
        EXPECT_EQ(CallsIn(*_renderer, DrawKind::Geometry, easy.rect).size(), 1u);
        EXPECT_EQ(Get<UIDropdown>("Level").box, UIBoxStyle{}) << "the header's own box is untouched";
    }

    // ---- composed options: an element with a clicker is an option ----------------------------------------

    TEST_F(UIDropdownTest, TheHeaderShowsTheCaptionOfAChosenComposedOption) {
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Comp")), "Pick") << "the placeholder while nothing is chosen";

        SetVar("comp", "easy");
        Draw();
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Comp")), "Easy") << "the text component of the option itself";
        EXPECT_EQ(Get<UIDropdown>("Comp").FindChosenOption(), &Element("CompEasy"));
        EXPECT_TRUE(Element("CompEasy").selected);
        EXPECT_FALSE(Element("CompHard").selected);

        SetVar("comp", "hard");
        Draw();
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Comp")), "Hard") << "the caption child of an icon-left option";
        EXPECT_EQ(Get<UIDropdown>("Comp").FindChosenOption(), &Element("CompHard"));

        SetVar("comp", "nothing like it");
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Comp")), "Pick");
    }

    TEST_F(UIDropdownTest, ALanguageSwitchRewordsTheHeaderOfAComposedChoice) {
        SetVar("comp", "hard");
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Comp")), "Hard");

        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("ru"_intern));
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Comp")), "Сложно");
        SetVar("comp", "easy");
        EXPECT_EQ(HeaderText(Get<UIDropdown>("Comp")), "Легко");
    }

    TEST_F(UIDropdownTest, ClosedTheComposedOptionsAndTheirChildrenHaveNoRectAndDrawNothing) {
        for (const char* name : {"CompEasy", "CompHard", "CompHardIcon", "CompHardCaption", "CompLocked"}) {
            EXPECT_TRUE(Element(name).rect.IsEmpty()) << name;
        }
        EXPECT_TRUE(_canvas->GetLayout().OpenPopups().empty());
        EXPECT_EQ(GlyphQuadsIn({0.0f, 256.0f, 800.0f, 168.0f}), 0u) << "where the list would be";
    }

    TEST_F(UIDropdownTest, OpenTheComposedOptionsLieInAColumn) {
        Get<UIDropdown>("Comp").Open();
        Draw();

        const float tops[] = {256.0f, 312.0f, 368.0f};
        const char* const names[] = {"CompEasy", "CompHard", "CompLocked"};
        for (size_t i = 0; i < 3; ++i) {
            const Rect& rect = Element(names[i]).rect;
            EXPECT_FLOAT_EQ(rect.x, 520.0f) << names[i];
            EXPECT_FLOAT_EQ(rect.y, tops[i]) << names[i];
            EXPECT_FLOAT_EQ(rect.w, 240.0f) << names[i];
            EXPECT_FLOAT_EQ(rect.h, 56.0f) << names[i] << ": optionHeight";
        }

        // The children of the icon-left option follow the rect the list gave it
        const Rect& hard = Element("CompHard").rect;
        const Rect& icon = Element("CompHardIcon").rect;
        const Rect& caption = Element("CompHardCaption").rect;
        EXPECT_FALSE(icon.IsEmpty());
        EXPECT_FALSE(caption.IsEmpty());
        EXPECT_GE(icon.x, hard.x);
        EXPECT_LE(icon.x + icon.w, caption.x + 1e-2f) << "the icon is left of the caption";
        EXPECT_LE(caption.x + caption.w, hard.x + hard.w + 1e-2f);
        EXPECT_GE(caption.y, hard.y - 1e-2f);
        EXPECT_LE(caption.y + caption.h, hard.y + hard.h + 1e-2f);
    }

    TEST_F(UIDropdownTest, AComposedListNearTheBottomEdgeOpensAbove) {
        Get<UIDropdown>("Both").Open();
        Draw();

        EXPECT_FLOAT_EQ(Element("BothButton").rect.y, 338.0f);
        EXPECT_FLOAT_EQ(Element("BothComposed").rect.y, 394.0f);
    }

    TEST_F(UIDropdownTest, TheCaptionsOfTheComposedOptionsAreInTheOpenDrawOfTheirOptions) {
        Get<UIDropdown>("Comp").Open();
        Draw();

        EXPECT_GT(GlyphQuadsIn(Element("CompEasy").rect), 0u);
        EXPECT_GT(GlyphQuadsIn(Element("CompHardCaption").rect), 0u);
        EXPECT_GT(GlyphQuadsIn(Element("CompLocked").rect), 0u);

        Get<UIDropdown>("Comp").Close();
        Draw();
        EXPECT_EQ(GlyphQuadsIn({0.0f, 256.0f, 800.0f, 168.0f}), 0u);
    }

    TEST_F(UIDropdownTest, ThePopupDrawsAComposedOptionInItsOwnState) {
        auto& comp = Get<UIDropdown>("Comp");
        SetVar("comp", "easy");
        comp.Open();
        Draw();
        EXPECT_TRUE(Element("CompEasy").GetVisualState().selected);
        EXPECT_FALSE(Element("CompHard").GetVisualState().selected);
        EXPECT_TRUE(Element("CompHardCaption").GetVisualState().selected == false) << "a child follows the option it is in";

        comp.disabled = true;
        Draw();
        EXPECT_TRUE(Element("CompEasy").groupDisabled) << "a disabled dropdown greys its options";
        EXPECT_FALSE(comp.IsOpen());
    }

    TEST_F(UIDropdownTest, AClickOnAComposedOptionEmitsTheActionOfTheDropdownWithTheValueOfTheClicker) {
        ActionLog log;
        Get<UIDropdown>("Comp").Open();
        Draw();

        EXPECT_TRUE(Click("CompHard"));

        ASSERT_EQ(log.Total(), 1u);
        EXPECT_EQ(log.LastAction(), "DdTestComp"_intern);
        EXPECT_EQ(log.LastValue(), "hard"_intern);
        EXPECT_FALSE(Get<UIDropdown>("Comp").IsOpen());
        Draw();
        EXPECT_TRUE(Element("CompHard").rect.IsEmpty());
        EXPECT_TRUE(Element("CompHardCaption").rect.IsEmpty());
    }

    TEST_F(UIDropdownTest, AClickOnTheCaptionChildOfAnOptionChoosesTheOption) {
        ActionLog log;
        Get<UIDropdown>("Comp").Open();
        Draw();

        EXPECT_TRUE(Click("CompHardCaption"));

        ASSERT_EQ(log.Total(), 1u);
        EXPECT_EQ(log.LastValue(), "hard"_intern);
    }

    TEST_F(UIDropdownTest, AChoiceChangesNoVariableOnAComposedList) {
        Get<UIDropdown>("Comp").Open();
        Draw();
        EXPECT_TRUE(Click("CompEasy"));
        EXPECT_FALSE(CoreManager::GetUIVars().Has("comp"_intern));
    }

    TEST_F(UIDropdownTest, ADisabledComposedOptionTakesTheClickAndTheListStaysOpen) {
        ActionLog log;
        Get<UIDropdown>("Comp").Open();
        Draw();

        EXPECT_TRUE(Click("CompLocked"));

        EXPECT_EQ(log.Total(), 0u);
        EXPECT_TRUE(Get<UIDropdown>("Comp").IsOpen());
        EXPECT_TRUE(Click("CompEasy"));
        EXPECT_EQ(log.Total(), 1u) << "the option beside it still works";
    }

    TEST_F(UIDropdownTest, ADisabledDropdownOfComposedOptionsNeverOpensAndAnOpenOneCloses) {
        auto& comp = Get<UIDropdown>("Comp");
        comp.disabled = true;
        EXPECT_TRUE(Click("Comp"));
        EXPECT_FALSE(comp.IsOpen());

        comp.disabled = false;
        comp.Open();
        Draw();
        ASSERT_TRUE(comp.IsOpen());
        comp.disabled = true;
        Draw();
        EXPECT_FALSE(comp.IsOpen());
        EXPECT_TRUE(Element("CompEasy").rect.IsEmpty());
    }

    TEST_F(UIDropdownTest, EmitsOnChoiceAsksTheDisabledStateOfAnyOption) {
        const auto& comp = Get<UIDropdown>("Comp");
        EXPECT_TRUE(comp.EmitsOnChoice(Element("CompEasy")));
        EXPECT_FALSE(comp.EmitsOnChoice(Element("CompLocked")));
        EXPECT_FALSE(Get<UIDropdown>("Silent").EmitsOnChoice(Element("SilentA"))) << "no action";
        EXPECT_FALSE(Get<UIDropdown>("Level").EmitsOnChoice(Element("Locked")));
        EXPECT_TRUE(Get<UIDropdown>("Level").EmitsOnChoice(Element("Easy")));
    }

    TEST_F(UIDropdownTest, APressOutsideAComposedListClosesItAndEmitsNothing) {
        ActionLog log;
        Get<UIDropdown>("Comp").Open();
        Draw();

        EXPECT_TRUE(ClickAt(100.0f, 580.0f));

        EXPECT_FALSE(Get<UIDropdown>("Comp").IsOpen());
        EXPECT_EQ(log.Total(), 0u);
    }

    TEST_F(UIDropdownTest, AListOfACaptionOptionAndAnIconOptionChoosesAndShowsBoth) {
        ActionLog log;
        auto& both = Get<UIDropdown>("Both");
        SetVar("both", "button");
        Draw();
        EXPECT_EQ(HeaderText(both), "Medium");
        SetVar("both", "composed");
        EXPECT_EQ(HeaderText(both), "Composed");
        EXPECT_EQ(both.FindChosenOption(), &Element("BothComposed"));

        both.Open();
        Draw();
        EXPECT_TRUE(Click("BothButton"));
        EXPECT_EQ(log.LastValue(), "button"_intern);
        both.Open();
        Draw();
        EXPECT_TRUE(Click("BothComposed"));
        EXPECT_EQ(log.LastAction(), "DdTestBoth"_intern);
        EXPECT_EQ(log.LastValue(), "composed"_intern);
        EXPECT_EQ(log.Total(), 2u);
    }

    TEST_F(UIDropdownTest, AnOpenComposedListIsValidatedAgainstTheTouchSize) {
        Get<UIDropdown>("Comp").Open();
        Draw();
        EXPECT_TRUE(_canvas->GetLayout().Validate({}).empty());

        Get<UIDropdown>("Comp").optionHeight = 20.0f;
        _canvas->Invalidate();  // an edit of the tree is not noticed by the cache
        Draw();
        size_t small = 0;
        for (const auto& issue : _canvas->GetLayout().Validate({})) {
            small += issue.kind == UILayoutIssueKind::TouchTargetTooSmall ? 1u : 0u;
        }
        EXPECT_GE(small, 3u) << "the three composed options are under a touch target";
    }

}  // namespace BECore::Tests
