#include <BECore/Config/XmlConfig.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIDropdown.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
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
        //   Mixed (500, 20) holds a label between its options
        constexpr const char* kDropdownXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIDropdown" name="Level" bind="level" action="DdTestLevel" text="Pick one" x="20" y="20" width="240" height="56" optionHeight="56" color="40,40,70,255">
            <children>
              <element type="UIButton" name="Easy" text="{#lvl.easy}" value="easy" color="70,70,100,255"/>
              <element type="UIButton" name="Medium" text="{#lvl.medium}" value="medium" color="70,70,100,255"/>
              <element type="UIButton" name="Hard" text="{#lvl.hard}" value="hard" height="80" color="70,70,100,255"/>
              <element type="UIButton" name="Locked" text="Locked" value="locked" disabled="true" color="70,70,100,255"/>
            </children>
          </element>
          <element type="UIDropdown" name="Plain" bind="plain" action="DdTestPlain" x="300" y="20" width="160" height="56" color="40,40,70,255">
            <children>
              <element type="UIButton" name="PlainA" text="A" value="a" width="260"/>
              <element type="UIButton" name="PlainB" text="B" value="b"/>
            </children>
          </element>
          <element type="UIDropdown" name="Low" bind="low" action="DdTestLow" x="20" y="520" width="240" height="56" optionHeight="56" color="40,40,70,255">
            <children>
              <element type="UIButton" name="LowA" text="A" value="a"/>
              <element type="UIButton" name="LowB" text="B" value="b"/>
            </children>
          </element>
          <element type="UIDropdown" name="Off" bind="off" action="DdTestOff" x="300" y="200" width="240" height="56" optionHeight="56" disabled="true">
            <children>
              <element type="UIButton" name="OffA" text="A" value="a"/>
            </children>
          </element>
          <element type="UIDropdown" name="Silent" bind="silent" x="300" y="300" width="240" height="56" optionHeight="56">
            <children>
              <element type="UIButton" name="SilentA" text="A" value="a"/>
            </children>
          </element>
          <element type="UIDropdown" name="Mixed" bind="mixed" action="DdTestMixed" x="500" y="400" width="240" height="56" optionHeight="56">
            <children>
              <element type="UILabel" name="Caption" text="Caption" value="x"/>
              <element type="UIButton" name="MixedA" text="A" value="a"/>
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

            eastl::vector<PoolString> _actions;
            eastl::vector<PoolString> _values;
        };

        void SetVar(const char* name, const char* value) {
            CoreManager::GetUIVars().Set(PoolString::Intern(name), value);
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

    TEST_F(UIDropdownTest, ADropdownFromAFileIsAnInteractiveLabelWithItsOptionsAsChildren) {
        const auto& level = Get<UIDropdown>("Level");
        static_assert(std::is_base_of_v<UILabel, UIDropdown>);
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
        EXPECT_EQ(Get<UIDropdown>("Level").GetDisplayText(), "Pick one");
        EXPECT_EQ(Get<UIDropdown>("Plain").GetDisplayText(), "");

        SetVar("level", "nothing like it");
        EXPECT_EQ(Get<UIDropdown>("Level").GetDisplayText(), "Pick one") << "a value no option has chosen nothing";
    }

    TEST_F(UIDropdownTest, TheHeaderShowsTheTextOfTheChosenOption) {
        SetVar("level", "medium");
        Draw();

        EXPECT_EQ(Get<UIDropdown>("Level").GetDisplayText(), "Medium");
        EXPECT_EQ(Get<UIDropdown>("Level").FindChosenOption(), &Get<UIButton>("Medium"));
        EXPECT_TRUE(Get<UIButton>("Medium").selected);
        EXPECT_FALSE(Get<UIButton>("Easy").selected);

        SetVar("plain", "b");
        EXPECT_EQ(Get<UIDropdown>("Plain").GetDisplayText(), "B");
    }

    TEST_F(UIDropdownTest, ALanguageSwitchRewordsTheHeader) {
        SetVar("level", "hard");
        EXPECT_EQ(Get<UIDropdown>("Level").GetDisplayText(), "Hard");

        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("ru"_intern));
        EXPECT_EQ(Get<UIDropdown>("Level").GetDisplayText(), "Сложно");
    }

    TEST_F(UIDropdownTest, TheShownTextIsNeverWrittenIntoTheAuthoredText) {
        SetVar("level", "easy");
        Draw();
        EXPECT_EQ(Get<UIDropdown>("Level").GetDisplayText(), "Easy");
        EXPECT_EQ(Get<UIDropdown>("Level").text, "Pick one");

        CoreManager::GetUIVars().Remove("level"_intern);
        EXPECT_EQ(Get<UIDropdown>("Level").GetDisplayText(), "Pick one");
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
            EXPECT_TRUE(Get<UIButton>(name).rect.IsEmpty()) << name;
        }
        EXPECT_TRUE(_canvas->GetLayout().OpenPopups().empty());
    }

    TEST_F(UIDropdownTest, OpenTheOptionHeightComesFromTheOptionThenTheDropdownThenTheHeader) {
        Get<UIDropdown>("Plain").Open();
        Draw();
        // No optionHeight, so the header's 56; PlainA asks for 260 units of width, which makes the column that wide
        EXPECT_FLOAT_EQ(Get<UIButton>("PlainA").rect.h, 56.0f);
        EXPECT_FLOAT_EQ(Get<UIButton>("PlainB").rect.h, 56.0f);
        EXPECT_FLOAT_EQ(Get<UIButton>("PlainA").rect.w, 260.0f);
        EXPECT_FLOAT_EQ(Get<UIButton>("PlainB").rect.w, 260.0f) << "one column";
        EXPECT_FLOAT_EQ(Get<UIButton>("PlainA").rect.y, 76.0f);
        EXPECT_FLOAT_EQ(Get<UIButton>("PlainB").rect.y, 132.0f);

        Get<UIDropdown>("Level").Open();  // the newest popup wins, Plain is closed
        Draw();
        EXPECT_FALSE(Get<UIDropdown>("Plain").IsOpen());
        EXPECT_FLOAT_EQ(Get<UIButton>("Easy").rect.h, 56.0f) << "optionHeight of the dropdown";
        EXPECT_FLOAT_EQ(Get<UIButton>("Hard").rect.h, 80.0f) << "the option's own height wins";
        EXPECT_FLOAT_EQ(Get<UIButton>("Easy").rect.y, 76.0f);
        EXPECT_FLOAT_EQ(Get<UIButton>("Medium").rect.y, 132.0f);
        EXPECT_FLOAT_EQ(Get<UIButton>("Hard").rect.y, 188.0f);
        EXPECT_FLOAT_EQ(Get<UIButton>("Locked").rect.y, 268.0f);
        EXPECT_FLOAT_EQ(Get<UIButton>("Easy").rect.w, 240.0f);
    }

    TEST_F(UIDropdownTest, AListNearTheBottomEdgeOpensAboveTheHeader) {
        Get<UIDropdown>("Low").Open();
        Draw();

        EXPECT_FLOAT_EQ(Get<UIButton>("LowA").rect.y, 408.0f);
        EXPECT_FLOAT_EQ(Get<UIButton>("LowB").rect.y, 464.0f);
    }

    TEST_F(UIDropdownTest, OnlyTheButtonsAreOptions) {
        auto& mixed = Get<UIDropdown>("Mixed");
        mixed.Open();
        SetVar("mixed", "x");  // the label has a `value` field in the file, but it is no option
        Draw();

        EXPECT_EQ(mixed.FindChosenOption(), nullptr);
        EXPECT_TRUE(Get<UILabel>("Caption").rect.IsEmpty());
        EXPECT_FLOAT_EQ(Get<UIButton>("MixedA").rect.y, 456.0f) << "the label takes no room in the list";
    }

    // ---- input ------------------------------------------------------------------------------------------------------------

    TEST_F(UIDropdownTest, AClickOnTheHeaderOpensTheListAndAnotherClosesIt) {
        ActionLog log;
        EXPECT_TRUE(Click("Level"));
        EXPECT_TRUE(Get<UIDropdown>("Level").IsOpen());
        Draw();
        EXPECT_FALSE(Get<UIButton>("Easy").rect.IsEmpty());

        EXPECT_TRUE(Click("Level"));
        EXPECT_FALSE(Get<UIDropdown>("Level").IsOpen());
        Draw();
        EXPECT_TRUE(Get<UIButton>("Easy").rect.IsEmpty());
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
        EXPECT_TRUE(Get<UIButton>("Medium").rect.IsEmpty());
    }

    TEST_F(UIDropdownTest, AChoiceChangesNoVariable) {
        SetVar("level", "easy");
        Get<UIDropdown>("Level").Open();
        Draw();
        ASSERT_TRUE(Click("Hard"));

        EXPECT_EQ(CoreManager::GetUIVars().Get("level"_intern), "easy") << "the game answers the action, the dropdown does not";
        Draw();
        EXPECT_EQ(Get<UIDropdown>("Level").GetDisplayText(), "Easy");
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
        ASSERT_FALSE(Get<UIButton>("Easy").rect.IsEmpty());

        level.disabled = true;
        Draw();  // the canvas notices the list is gone and invalidates
        Draw();

        EXPECT_FALSE(level.IsOpen());
        EXPECT_TRUE(Get<UIButton>("Easy").rect.IsEmpty());
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
        EXPECT_TRUE(Get<UIButton>("Easy").rect.IsEmpty());
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

}  // namespace BECore::Tests
