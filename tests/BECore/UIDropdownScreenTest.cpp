#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UIStack.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UIVars.h>
#include <Common/Printers.h>
#include <Common/TestText.h>
#include <Common/UIScreenTestSupport.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The screen test of a panel with a UIDropdown, the template for a screen with a list of many values (.claude/rules/ui-layout.md, "A screen test").
     * The shape is the one of UISettingsScreenTest: a HUD canvas with an `Open` button and a modal `Levels` canvas started hidden (a dim, a plate, a title, the dropdown `Level` with
     * five options, an OK button). The options are `{#key}` words; the variable `level` holds the longest of them, so the header is as wide as it gets.
     *
     * The matrix runs once per language and **twice: closed and open**. An open list is checked as the validator checks any element (size, safe area, touch target) and, for the
     * words, by `CheckContent()` of the header and of every option: an ellipsis label "can shrink", so the validator alone does not see a cut word. A broken copy of each check
     * shows that the check can fail.
     */

    namespace {

        constexpr float kViewWidth = 840.0f;
        constexpr float kViewHeight = 800.0f;
        constexpr float kPortraitWidth = 560.0f;
        constexpr float kPortraitHeight = 840.0f;
        constexpr size_t kOptionCount = 5;
        constexpr size_t kLanguageCount = 3;

        // Plate: 16 + 44 (title) + 12 + 56 (the dropdown) + 12 + 56 (OK) + 16 = 212, taller by 8 for the rounding of a stack: 220. The options have no height of their own: `optionHeight`.
        constexpr const char* kLevelsXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main" viewWidth="@VIEW_W@" viewHeight="@VIEW_H@"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="SceneView" useSafeArea="true">
        <elements>
          <element type="UIElement" name="Open" anchor="TopRight" x="-12" y="12" width="190" height="56" fontSize="24" color="45,75,120,255">
            <components>
              <component type="UITextComponent" text="{#levels.open}" align="Center" verticalAlign="Center" wrap="false"/>
              <component type="UIInputClicker" action="LevelsOpen"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
    <node name="Levels" layer="20"><components>
      <component type="UICanvasComponent" space="SceneView" useSafeArea="true" modal="true" visible="false">
        <elements>
          <element type="UIElement" name="Dim" anchor="Stretch" ignoreSafeArea="true" color="0,0,0,150" blocksInput="true"/>
          <element type="UIStack" name="Plate" anchor="Center" width="94%" maxWidth="640" height="220" direction="Vertical" spacing="12" padding="16" crossAlign="Stretch" color="36,40,52,255" blocksInput="true">
            <children>
              <element type="UIElement" name="Title" height="44" fontSize="30">
                <components><component type="UITextComponent" text="{#levels.title}" align="Center"/></components>
              </element>
              <element type="UIDropdown" name="Level" bind="level" action="SetLevel" height="56" optionHeight="56" fontSize="24" color="70,70,100,255">
                <components><component type="UITextComponent" text="{#levels.pick}" verticalAlign="Center" wrap="false" fadeWhenDisabled="true"/></components>
                <children>
                  <element type="UIElement" name="LevelNovice" fontSize="24" color="70,70,100,255">
                    <components>
                      <component type="UITextComponent" text="{#level.novice}" align="Center" verticalAlign="Center" wrap="false"/>
                      <component type="UIInputClicker" value="novice"/>
                    </components>
                  </element>
                  <element type="UIElement" name="LevelEasy" fontSize="24" color="70,70,100,255">
                    <components>
                      <component type="UITextComponent" text="{#level.easy}" align="Center" verticalAlign="Center" wrap="false"/>
                      <component type="UIInputClicker" value="easy"/>
                    </components>
                  </element>
                  <element type="UIElement" name="LevelNormal" fontSize="24" color="70,70,100,255">
                    <components>
                      <component type="UITextComponent" text="{#level.normal}" align="Center" verticalAlign="Center" wrap="false"/>
                      <component type="UIInputClicker" value="normal"/>
                    </components>
                  </element>
                  <element type="UIElement" name="LevelHard" fontSize="24" color="70,70,100,255">
                    <components>
                      <component type="UITextComponent" text="{#level.hard}" align="Center" verticalAlign="Center" wrap="false"/>
                      <component type="UIInputClicker" value="hard"/>
                    </components>
                  </element>
                  <element type="UIElement" name="LevelMaster" fontSize="24" color="70,70,100,255">
                    <components>
                      <component type="UITextComponent" text="{#level.master}" align="Center" verticalAlign="Center" wrap="false"/>
                      <component type="UIInputClicker" value="master"/>
                    </components>
                  </element>
                </children>
              </element>
              <element type="UIElement" name="Ok" height="56" fontSize="26" color="40,90,60,255">
                <components>
                  <component type="UITextComponent" text="{#levels.ok}" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="LevelsClose"/>
                </components>
              </element>
            </children>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        // English, Russian and a deliberately long test language. The longest option is `master` in all three
        constexpr const char* kStringsXml = R"(
<UIStrings default="en">
  <language id="en">
    <string key="levels.open" text="Levels"/>
    <string key="levels.title" text="Difficulty"/>
    <string key="levels.pick" text="Pick a level"/>
    <string key="levels.ok" text="OK"/>
    <string key="level.novice" text="Novice"/>
    <string key="level.easy" text="Easy"/>
    <string key="level.normal" text="Normal"/>
    <string key="level.hard" text="Hard"/>
    <string key="level.master" text="Grandmaster"/>
  </language>
  <language id="ru">
    <string key="levels.open" text="Уровни"/>
    <string key="levels.title" text="Сложность"/>
    <string key="levels.pick" text="Выберите уровень"/>
    <string key="levels.ok" text="Готово"/>
    <string key="level.novice" text="Новичок"/>
    <string key="level.easy" text="Лёгкий"/>
    <string key="level.normal" text="Обычный"/>
    <string key="level.hard" text="Трудный"/>
    <string key="level.master" text="Гроссмейстер"/>
  </language>
  <language id="de">
    <string key="levels.open" text="Schwierigkeiten"/>
    <string key="levels.title" text="Schwierigkeitsgrad wählen"/>
    <string key="levels.pick" text="Bitte einen Grad wählen"/>
    <string key="levels.ok" text="Übernehmen"/>
    <string key="level.novice" text="Absoluter Anfänger"/>
    <string key="level.easy" text="Ziemlich einfach"/>
    <string key="level.normal" text="Normale Schwierigkeit"/>
    <string key="level.hard" text="Ziemlich schwierig"/>
    <string key="level.master" text="Großmeisterliches Niveau"/>
  </language>
</UIStrings>)";

        constexpr const char* kOptionKeys[] = {"level.novice", "level.easy", "level.normal", "level.hard", "level.master"};
        constexpr const char* kOptionNames[] = {"LevelNovice", "LevelEasy", "LevelNormal", "LevelHard", "LevelMaster"};

        /// The second option with an icon on its left: an image child and a caption child under the clicker (the other four are a caption on the element and a clicker)
        void GiveEasyAnIcon(String& xml) {
            const size_t begin = xml.find("<element type=\"UIElement\" name=\"LevelEasy\"");
            const size_t endTag = xml.find("</element>", begin);
            ASSERT_NE(begin, String::npos);
            ASSERT_NE(endTag, String::npos);
            const String icon =
                "<element type=\"UIElement\" name=\"LevelEasy\" fontSize=\"24\" color=\"70,70,100,255\" layout=\"Stack\" direction=\"Horizontal\" padding=\"8,0,0,0\" spacing=\"6\" "
                "crossAlign=\"Center\"><components><component type=\"UIInputClicker\" value=\"easy\"/></components><children>"
                "<element type=\"UIElement\" name=\"LevelEasyIcon\" height=\"50%\" aspectRatio=\"1\"><components><component type=\"UIImageComponent\"/></components></element>"
                "<element type=\"UIElement\" name=\"LevelEasyCaption\" flex=\"1\" height=\"100%\"><components><component type=\"UITextComponent\" text=\"{#level.easy}\" align=\"Center\" "
                "verticalAlign=\"Center\" overflow=\"Ellipsis\" wrap=\"false\"/></components></element></children></element>";
            xml.replace(begin, endTag + String("</element>").size() - begin, icon);
        }

        /// The part of the game: it shows and hides the panel and answers a choice with the variable the dropdown is bound to. It records every action with its value
        class LevelsGame : public SubscriptionHolder {
        public:
            explicit LevelsGame(Scene& scene) : _scene(scene) {
                Subscribe<InputEvents::ActionEvent, &LevelsGame::OnAction>(this);
            }

            size_t Total() const {
                return _actions.size();
            }
            size_t CountOf(const char* action) const {
                size_t count = 0;
                for (const PoolString& recorded : _actions) {
                    count += recorded == PoolString::Intern(action) ? 1u : 0u;
                }
                return count;
            }
            PoolString LastValue() const {
                return _values.empty() ? PoolString{} : _values.back();
            }

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                _actions.push_back(event.action);
                _values.push_back(event.value);
                if (event.action == "LevelsOpen"_intern) {
                    UICanvasComponent::SetVisibleByName(_scene, "Levels"_intern, true);
                } else if (event.action == "LevelsClose"_intern) {
                    UICanvasComponent::SetVisibleByName(_scene, "Levels"_intern, false);
                } else if (event.action == "SetLevel"_intern) {
                    CoreManager::GetUIVars().Set("level"_intern, event.value.ToStringView());
                }
            }

            Scene& _scene;
            Vector<PoolString> _actions;
            Vector<PoolString> _values;
        };

    }  // namespace

    class UIDropdownScreenTest : public UIPanelScreenTest {
    protected:
        UIDropdownScreenTest() : UIPanelScreenTest("Levels"_intern) {}

        void SetUp() override {
            UIPanelScreenTest::SetUp();
            LoadStrings(kStringsXml, kLanguageCount);
            CoreManager::GetUIVars().Set("level"_intern, "master");
        }

        void TearDown() override {
            _game.reset();
            UIPanelScreenTest::TearDown();
        }

        void LoadScene(float viewWidth = kViewWidth, float viewHeight = kViewHeight, FunctionRef<void(String&)> edit = NoTextEdit) {
            _game.reset();
            ASSERT_NO_FATAL_FAILURE(LoadPanelScene(SceneXml(kLevelsXml, viewWidth, viewHeight, edit), viewWidth, viewHeight));
            _game.emplace(GetScene());
        }

        /// The panel shown, the list opened by a click on the header, and drawn: the state the rest of the test looks at
        void ShowAndOpen() {
            ASSERT_NO_FATAL_FAILURE(Show());
            ASSERT_TRUE(Click(Get<UIDropdown>("Level")));
            Draw();
            ASSERT_TRUE(Get<UIDropdown>("Level").IsOpen());
            ASSERT_FALSE(El("LevelMaster").rect.IsEmpty());
        }

        Optional<LevelsGame> _game;
    };

    // ---- the size matrix, once per language, closed and open ------------------------------------------------------------

    TEST_F(UIDropdownScreenTest, TheClosedPanelPassesTheSizeMatrixInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        const auto failures = ValidatePanel(*_panel);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        const auto hudFailures = ValidatePanel(*_hud);
        EXPECT_TRUE(hudFailures.empty()) << DescribeMatrixFailures(hudFailures).c_str();
    }

    TEST_F(UIDropdownScreenTest, TheOpenPanelPassesTheSizeMatrixInEveryLanguageInALandscapeAndAPortraitView) {
        for (const auto [width, height] : {Pair<float, float>{kViewWidth, kViewHeight}, Pair<float, float>{kPortraitWidth, kPortraitHeight}}) {
            ASSERT_NO_FATAL_FAILURE(LoadScene(width, height));
            ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
            const auto failures = ValidatePanel(*_panel);
            EXPECT_TRUE(failures.empty()) << width << " x " << height << ": " << DescribeMatrixFailures(failures).c_str();
        }
    }

    TEST_F(UIDropdownScreenTest, NoWordIsCutClosedOrOpenOnAnyCaseOfTheMatrixInAnyLanguage) {
        for (const auto [width, height] : {Pair<float, float>{kViewWidth, kViewHeight}, Pair<float, float>{kPortraitWidth, kPortraitHeight}}) {
            ASSERT_NO_FATAL_FAILURE(LoadScene(width, height));
            ASSERT_NO_FATAL_FAILURE(Show());
            const String closed = ProblemsOnMatrix(*_panel);
            EXPECT_TRUE(closed.empty()) << width << " closed: " << closed.c_str();

            ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
            const String open = ProblemsOnMatrix(*_panel);
            EXPECT_TRUE(open.empty()) << width << " open: " << open.c_str();
        }
    }

    TEST_F(UIDropdownScreenTest, EveryOptionAndTheHeaderAreAskedInEveryLanguage) {
        // The same walk as Problems(), written out for the open list: the header and the five options, each with the width it got
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        UICanvasLayout& layout = _panel->GetLayout();
        size_t languages = 0;
        ForEachLanguage([&](PoolString language) {
            ++languages;
            layout.Invalidate();
            Draw();
            const float scale = layout.GetResolved().scale;
            const UIDropdown& header = Get<UIDropdown>("Level");
            const UIContentFit headerFit = header.CheckContent(header.rect.w, header.rect.h, scale);
            EXPECT_TRUE(headerFit.hasContent) << language.ToStringView().data();
            EXPECT_TRUE(headerFit.fits) << language.ToStringView().data() << " header '" << ShownText(header).c_str() << "'";
            for (const char* name : kOptionNames) {
                const UIElement& option = El(name);
                EXPECT_FALSE(option.rect.IsEmpty()) << name;
                const UIContentFit fit = option.CheckContent(option.rect.w - 2.0f * kButtonAir, option.rect.h, scale);
                EXPECT_TRUE(fit.hasContent && fit.fits) << language.ToStringView().data() << " " << name << " '" << ShownText(option).c_str() << "'";
            }
        });
        EXPECT_EQ(languages, 3u) << "en, ru and de";
    }

    TEST_F(UIDropdownScreenTest, TheLongestOptionIsMasterInEveryLanguageAndTheLongLanguageIsLonger) {
        UIStrings& strings = CoreManager::GetUIStrings();
        size_t english = 0;
        size_t german = 0;
        ForEachLanguage([&](PoolString language) {
            size_t longest = 0;
            for (const char* key : kOptionKeys) {
                longest = std::max(longest, Characters(*strings.Find(key)));
            }
            EXPECT_EQ(longest, Characters(*strings.Find("level.master"))) << language.ToStringView().data();
        });
        for (const char* key : kOptionKeys) {
            ASSERT_TRUE(strings.SetLanguage("en"_intern));
            english += Characters(*strings.Find(key));
            ASSERT_TRUE(strings.SetLanguage("de"_intern));
            german += Characters(*strings.Find(key));
        }
        strings.SetLanguage("en"_intern);
        EXPECT_GE(static_cast<float>(german), 1.4f * static_cast<float>(english));
    }

    // ---- the list stays inside the safe area ------------------------------------------------------------------------------

    TEST_F(UIDropdownScreenTest, TheListStaysInsideTheSafeAreaAndAFullListFlipsAboveAHeaderNearTheBottom) {
        // The plate at the bottom edge: the five options do not fit under the header, they fit above it
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](String& xml) { Replace(xml, "name=\"Plate\" anchor=\"Center\"", "name=\"Plate\" anchor=\"Bottom\""); }));
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        const UIDropdown& header = Get<UIDropdown>("Level");
        for (const char* name : kOptionNames) {
            EXPECT_LE(El(name).rect.y + El(name).rect.h, header.rect.y + 0.01f) << name << " is above the header";
            EXPECT_GE(El(name).rect.y, 0.0f) << name;
        }
        const auto failures = ValidatePanel(*_panel);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(UIDropdownScreenTest, TheListIsUnderTheHeaderAndAboveTheOkButtonItCovers) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        const UIDropdown& header = Get<UIDropdown>("Level");
        EXPECT_NEAR(El("LevelNovice").rect.y, header.rect.y + header.rect.h, 0.01f);
        EXPECT_GE(El("LevelMaster").rect.w, header.rect.w - 0.01f);
        EXPECT_GT(El("LevelMaster").rect.y + El("LevelMaster").rect.h, El("Ok").rect.y) << "the list covers the OK button";
        EXPECT_EQ(_panel->GetLayout().HitTest(CenterOf(El("LevelEasy")).first, CenterOf(El("LevelEasy")).second), &El("LevelEasy"));
    }

    // ---- the game's part ----------------------------------------------------------------------------------------------------

    TEST_F(UIDropdownScreenTest, AChoiceIsAnActionTheGameAnswersWithTheVariableAndTheHeaderFollows) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        EXPECT_EQ(ShownText(Get<UIDropdown>("Level")), String("Grandmaster"));

        ASSERT_TRUE(Click(El("LevelHard")));
        EXPECT_EQ(_game->CountOf("SetLevel"), 1u);
        EXPECT_EQ(_game->LastValue(), "hard"_intern);
        EXPECT_EQ(_game->CountOf("LevelsClose"), 0u) << "the option took the click, not the OK button under it";
        EXPECT_FALSE(Get<UIDropdown>("Level").IsOpen());

        Draw();
        EXPECT_EQ(ShownText(Get<UIDropdown>("Level")), String("Hard"));
        EXPECT_TRUE(El("LevelEasy").rect.IsEmpty()) << "closed again";
    }

    TEST_F(UIDropdownScreenTest, APressOutsideTheListClosesItAndNothingElseHappens) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        const size_t before = _game->Total();
        ASSERT_TRUE(Click(El("Title")));
        EXPECT_FALSE(Get<UIDropdown>("Level").IsOpen());
        EXPECT_EQ(_game->Total(), before);
    }

    // ---- broken copies: each check must be able to fail ----------------------------------------------------------------------

    TEST_F(UIDropdownScreenTest, AnOptionOfThirtyUnitsIsReportedByTheValidatorAndByProblems) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](String& xml) { Replace(xml, "name=\"LevelEasy\"", "name=\"LevelEasy\" height=\"30\""); }));
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        EXPECT_FALSE(ValidatePanel(*_panel).empty());
        EXPECT_NE(Problems(_panel->GetLayout()).find("LevelEasy is 30.0 units high"), String::npos);
    }

    TEST_F(UIDropdownScreenTest, AWordThatIsTooLongIsReportedByProblemsAndNotByTheValidator) {
        String xml = kStringsXml;
        Replace(xml, "text=\"Grandmaster\"", "text=\"Grandmaster of the Eastern Wind Tournament Circuit, the Final Round, with Every Rule Enabled\"");
        ASSERT_NO_FATAL_FAILURE(LoadStrings(xml, kLanguageCount));
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        const String problems = ProblemsOnMatrix(*_panel);
        EXPECT_NE(problems.find("cuts 'Grandmaster of"), String::npos) << problems.c_str();
        EXPECT_TRUE(ValidatePanel(*_panel).empty()) << "an ellipsis label can shrink: only CheckContent sees it";
    }

    TEST_F(UIDropdownScreenTest, AListThatCannotFitIsReportedByTheValidator) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](String& xml) { Replace(xml, "optionHeight=\"56\"", "optionHeight=\"200\""); }));
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        const auto failures = ValidatePanel(*_panel);
        ASSERT_FALSE(failures.empty());
        bool outside = false;
        for (const UIMatrixFailure& failure : failures) {
            for (const UILayoutIssue& issue : failure.issues) {
                outside = outside || issue.kind == UILayoutIssueKind::OutsideCanvas || issue.kind == UILayoutIssueKind::OutsideSafeArea;
            }
        }
        EXPECT_TRUE(outside);
    }

    TEST_F(UIDropdownScreenTest, AHeaderWithNoHeightIsReportedByProblems) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](String& xml) { Replace(xml, "height=\"56\" optionHeight=\"56\"", "optionHeight=\"56\""); }));
        ASSERT_NO_FATAL_FAILURE(Show());
        const String problems = Problems(_panel->GetLayout());
        EXPECT_NE(problems.find("Level 'Grandmaster' has an empty rect"), String::npos) << problems.c_str();
    }

    // ---- an option with an icon: the same panel, the same findings ------------------------------------------------

    TEST_F(UIDropdownScreenTest, AnIconOptionPassesTheSizeMatrixClosedAndOpenInEveryLanguageInBothViews) {
        for (const auto [width, height] : {Pair<float, float>{kViewWidth, kViewHeight}, Pair<float, float>{kPortraitWidth, kPortraitHeight}}) {
            ASSERT_NO_FATAL_FAILURE(LoadScene(width, height, GiveEasyAnIcon));
            ASSERT_NO_FATAL_FAILURE(Show());
            const auto closed = ValidatePanel(*_panel);
            EXPECT_TRUE(closed.empty()) << width << " closed: " << DescribeMatrixFailures(closed).c_str();

            ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
            const auto open = ValidatePanel(*_panel);
            EXPECT_TRUE(open.empty()) << width << " open: " << DescribeMatrixFailures(open).c_str();
        }
    }

    TEST_F(UIDropdownScreenTest, NoWordOfAnIconOptionIsCutOnAnyCaseOfTheMatrixInAnyLanguage) {
        for (const auto [width, height] : {Pair<float, float>{kViewWidth, kViewHeight}, Pair<float, float>{kPortraitWidth, kPortraitHeight}}) {
            ASSERT_NO_FATAL_FAILURE(LoadScene(width, height, GiveEasyAnIcon));
            ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
            const String open = ProblemsOnMatrix(*_panel);
            EXPECT_TRUE(open.empty()) << width << " open: " << open.c_str();
        }
    }

    TEST_F(UIDropdownScreenTest, TheProblemsCheckSeesAComposedOptionAndItsCaptionChild) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](String& xml) {
            GiveEasyAnIcon(xml);
            Replace(xml, "name=\"LevelNovice\" fontSize=\"24\"", "name=\"LevelNovice\" height=\"30\" fontSize=\"24\"");
        }));
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        EXPECT_FALSE(ValidatePanel(*_panel).empty());
        EXPECT_NE(Problems(_panel->GetLayout()).find("LevelNovice is 30.0 units high"), String::npos);

        Vector<const UIElement*> controls;
        CollectControls(*_panel->GetLayout().root, controls);
        EXPECT_NE(eastl::find(controls.begin(), controls.end(), &El("LevelEasyCaption")), controls.end()) << "the caption child is checked on its own";
    }

    TEST_F(UIDropdownScreenTest, AnIconOptionHasTheRectOfAPlainOne) {
        for (const auto [width, height] : {Pair<float, float>{kViewWidth, kViewHeight}, Pair<float, float>{kPortraitWidth, kPortraitHeight}}) {
            ASSERT_NO_FATAL_FAILURE(LoadScene(width, height));
            ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
            Vector<Rect> buttons;
            for (const char* name : kOptionNames) {
                buttons.push_back(El(name).rect);
            }
            const Rect headerOfButtons = Get<UIDropdown>("Level").rect;

            ASSERT_NO_FATAL_FAILURE(LoadScene(width, height, GiveEasyAnIcon));
            ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
            EXPECT_FLOAT_EQ(Get<UIDropdown>("Level").rect.y, headerOfButtons.y);
            for (size_t i = 0; i < kOptionCount; ++i) {
                const Rect& composed = El(kOptionNames[i]).rect;
                EXPECT_FLOAT_EQ(composed.x, buttons[i].x) << kOptionNames[i];
                EXPECT_FLOAT_EQ(composed.y, buttons[i].y) << kOptionNames[i];
                EXPECT_FLOAT_EQ(composed.w, buttons[i].w) << kOptionNames[i];
                EXPECT_FLOAT_EQ(composed.h, buttons[i].h) << kOptionNames[i];
            }
        }
    }

    TEST_F(UIDropdownScreenTest, AChoiceOfAnIconOptionIsAnActionTheGameAnswersAndTheHeaderFollowsInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, GiveEasyAnIcon));
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        EXPECT_EQ(ShownText(Get<UIDropdown>("Level")), String("Grandmaster"));

        ASSERT_TRUE(Click(El("LevelEasy")));
        EXPECT_EQ(_game->CountOf("SetLevel"), 1u);
        EXPECT_EQ(_game->LastValue(), "easy"_intern);
        EXPECT_EQ(_game->CountOf("LevelsClose"), 0u) << "the option took the click, not the OK button under it";
        EXPECT_FALSE(Get<UIDropdown>("Level").IsOpen());

        Draw();
        EXPECT_TRUE(El("LevelEasy").rect.IsEmpty()) << "closed again";
        EXPECT_TRUE(El("LevelEasyCaption").rect.IsEmpty());
        ForEachLanguage([&](PoolString language) { EXPECT_EQ(ShownText(Get<UIDropdown>("Level")), String(*CoreManager::GetUIStrings().Find("level.easy"))) << language.ToStringView().data(); });
    }

    TEST_F(UIDropdownScreenTest, APressOutsideAListWithAnIconOptionClosesItAndNothingElseHappens) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, GiveEasyAnIcon));
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        const size_t before = _game->Total();
        ASSERT_TRUE(Click(El("Title")));
        EXPECT_FALSE(Get<UIDropdown>("Level").IsOpen());
        EXPECT_EQ(_game->Total(), before);
    }

}  // namespace BECore::Tests
