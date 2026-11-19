#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UIProgressBar.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UISliderComponent.h>
#include <BECore/UI/UIStack.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UISwitchComponent.h>
#include <BECore/UI/UIToggleComponent.h>
#include <BECore/UI/UIVars.h>
#include <BECore/UIScreenFixtures.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TestText.h>
#include <Common/UIScreenTestSupport.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The screen test of a settings panel, the template for a screen with controls and translations (.claude/rules/ui-layout.md, "A screen test").
     * The shape of the panel (the choice panel of UISelectorScreenTest with other rows):
     *
     *  - a small canvas `Hud` with one button, `Open`, whose action shows the panel;
     *  - the panel is a modal `SceneView` canvas on a node of its own (`Settings`), started hidden: a dim, a plate and rows, and an OK button. The rows: Sound (a toggle row: a caption and a switch,
     *    a `UIToggleComponent` on the row), Music volume (a caption and a slider element with a `UISliderComponent`, step 0.1), Download (a caption, a `UIProgressBar` and a percent label
     * `{progress}%`), Language (a caption and a `UISegmented` bound to `language`). Every text is a `{#key}` reference into the string table;
     *  - the game answers a toggle with the variable it is bound to, a slider with the volume, a segment with `UIStrings::SetLanguage` and the variable (`SettingsGame` below).
     *
     * **One run per language.** The validator reads the text that is shown, so a screen is checked once for every language of the table, and a new language added to the table
     * is covered without touching the test (`ForEachLanguage`). The table has `en`, `ru` and `de`, which is deliberately long (its captions together are 40 % or more longer than the English
     * ones, counted in characters, and a test holds that). **The validator is not enough for text:** a text with `overflow="Ellipsis"` (the default) "can shrink", so `TextDoesNotFit` is never
     * reported for it; `Problems()` asks `CheckContent()` of every control on the word that is shown, in every language, on every case of the matrix and in the narrowest window alone.
     *
     * **A row with `crossAlign="Stretch"` gives its children the height of the row**, an explicit `height` or none: a control that is meant to be a touch target of 56 units sits in a
     * row of 56 and the rows that hold a thin or short control (the slider, the progress bar) use `crossAlign="Center"`, so the height written in the file is the height that is laid
     * out and a broken copy (a slider of 30 units, one without a height) shows.
     */

    namespace {

        constexpr float kViewWidth = 840.0f;
        constexpr float kViewHeight = 800.0f;
        constexpr float kPortraitWidth = 560.0f;
        constexpr float kPortraitHeight = 840.0f;
        constexpr float kLongerShare = 1.4f;  // the long language is at least this much longer than the English one
        constexpr size_t kLanguageCount = 3;

        constexpr const char* kLanguageKeys[] = {"settings.title", "settings.sound", "settings.music", "settings.download", "settings.language", "settings.ok"};

        struct Language {
            const char* id;
            const char* title;    // what the title shows in it
            const char* segment;  // the name of its segment in the Language group
        };

        const Language kLanguages[] = {{"en", "Settings", "LanguageEn"}, {"ru", "Настройки", "LanguageRu"}, {"de", "Einstellungen ändern", "LanguageDe"}};

        constexpr const char* kTextControlNames[] = {"Title", "SoundCaption", "MusicCaption", "DownloadCaption", "Percent", "LanguageCaption", "LanguageEn", "LanguageRu", "LanguageDe", "Ok"};

        /// The part of the game: it shows and hides the panel, answers a toggle and a slider with the variable they are bound to and a language segment with
        /// `UIStrings::SetLanguage`. It records every action with its value
        class SettingsGame : public SubscriptionHolder {
        public:
            explicit SettingsGame(Scene& scene) : _scene(scene) {
                Subscribe<InputEvents::ActionEvent, &SettingsGame::OnAction>(this);
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
            /// The values that came with @p action, in order
            Vector<String> ValuesOf(const char* action) const {
                Vector<String> values;
                for (size_t i = 0; i < _actions.size(); ++i) {
                    if (_actions[i] == PoolString::Intern(action)) {
                        values.push_back(String(_values[i].ToStringView()));
                    }
                }
                return values;
            }

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                _actions.push_back(event.action);
                _values.push_back(event.value);
                if (event.action == "SettingsOpen"_intern) {
                    UICanvasComponent::SetVisibleByName(_scene, "Settings"_intern, true);
                } else if (event.action == "SettingsClose"_intern) {
                    UICanvasComponent::SetVisibleByName(_scene, "Settings"_intern, false);
                } else if (event.action == "SettingsSound"_intern) {
                    CoreManager::GetUIVars().Set("soundOn"_intern, event.value.ToStringView());
                } else if (event.action == "SettingsVolume"_intern) {
                    CoreManager::GetUIVars().Set("volume"_intern, event.value.ToStringView());
                } else if (event.action == "SettingsLanguage"_intern) {
                    if (CoreManager::GetUIStrings().SetLanguage(event.value)) {
                        CoreManager::GetUIVars().Set("language"_intern, event.value.ToStringView());
                    }
                }
            }

            Scene& _scene;
            Vector<PoolString> _actions;
            Vector<PoolString> _values;
        };

    }  // namespace

    class UISettingsScreenTest : public UIPanelScreenTest {
    protected:
        UISettingsScreenTest() : UIPanelScreenTest("Settings"_intern) {}

        void SetUp() override {
            UIPanelScreenTest::SetUp();
            LoadStrings(kSettingsStringsXml, kLanguageCount);
            SetVars("true", "0.5", "40", "en");
        }

        void TearDown() override {
            _game.reset();
            UIPanelScreenTest::TearDown();
        }

        static void SetVars(const char* soundOn, const char* volume, const char* progress, const char* language) {
            CoreManager::GetUIVars().Set("soundOn"_intern, soundOn);
            CoreManager::GetUIVars().Set("volume"_intern, volume);
            CoreManager::GetUIVars().Set("progress"_intern, progress);
            CoreManager::GetUIVars().Set("language"_intern, language);
        }

        /// The scene of the panel with a view of this size; the output has the same size, so a point of the view is a pixel
        void LoadScene(float viewWidth = kViewWidth, float viewHeight = kViewHeight, FunctionRef<void(String&)> edit = NoTextEdit) {
            _game.reset();
            const String scene = PanelSceneXml(kSettingsHudButton, "Settings", kSettingsPlate, kSettingsVariants);
            ASSERT_NO_FATAL_FAILURE(LoadPanelScene(SceneXml(scene, viewWidth, viewHeight, edit), viewWidth, viewHeight));
            _game.emplace(GetScene());
        }

        const UISliderComponent& Slider(const char* name) const {
            return ComponentOf<UISliderComponent>(name);
        }

        const UIToggleComponent& Toggle(const char* name) const {
            return ComponentOf<UIToggleComponent>(name);
        }

        /// The x of the slider's thumb centre when the value stands at @p fraction of the travel
        float XAt(float fraction) const {
            const UISliderComponent::Parts parts = UISliderComponent::GetParts(El("Volume").rect, fraction);
            return parts.travelStart + (parts.travelEnd - parts.travelStart) * fraction;
        }

        float VolumeY() const {
            return CenterOf(El("Volume")).second;
        }

        Optional<SettingsGame> _game;
    };

    // ---- the size matrix, once per language ------------------------------------------------------------------------------

    TEST_F(UISettingsScreenTest, ThePanelPassesTheSizeMatrixInALandscapeViewInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        const auto failures = ValidatePanel(*_panel);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        const auto hudFailures = ValidatePanel(*_hud);
        EXPECT_TRUE(hudFailures.empty()) << DescribeMatrixFailures(hudFailures).c_str();
    }

    TEST_F(UISettingsScreenTest, ThePanelPassesTheSizeMatrixInAPortraitViewInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kPortraitWidth, kPortraitHeight));
        const auto failures = ValidatePanel(*_panel);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(UISettingsScreenTest, NoWordIsCutOnAnyCaseOfTheMatrixInAnyLanguage) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        const String landscape = ProblemsOnMatrix(*_panel);
        EXPECT_TRUE(landscape.empty()) << landscape.c_str();
        const String hud = ProblemsOnMatrix(*_hud);
        EXPECT_TRUE(hud.empty()) << hud.c_str();

        ASSERT_NO_FATAL_FAILURE(LoadScene(kPortraitWidth, kPortraitHeight));
        const String portrait = ProblemsOnMatrix(*_panel);
        EXPECT_TRUE(portrait.empty()) << portrait.c_str();
    }

    TEST_F(UISettingsScreenTest, NoWordIsCutInTheNarrowestWindowAndEveryRowIsATouchTarget) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        UICanvasLayout& layout = _panel->GetLayout();
        LayOutInTheNarrowestWindow(layout);

        const float scale = kNarrowestWidth / kViewWidth;
        EXPECT_NEAR(layout.GetResolved().scale, scale, 0.01f);
        EXPECT_NEAR(layout.root->rect.w, kNarrowestWidth * (1.0f - 2.0f * UIPreview::kSideInsetShare) / scale, 0.5f) << "the safe area, in view units: about 689";

        const String problems = ProblemsInTheNarrowestWindow(*_panel);
        EXPECT_TRUE(problems.empty()) << problems.c_str();

        LayOutInTheNarrowestWindow(layout);
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        for (const char* name : {"Sound", "Volume", "LanguageEn", "LanguageRu", "LanguageDe", "Ok"}) {
            const UIElement* element = FindElement(name);
            ASSERT_NE(element, nullptr) << name;
            EXPECT_GE(element->rect.h, kScreenControlHeight - 0.01f) << name;
            EXPECT_GE(std::min(element->rect.w, element->rect.h) * layout.GetResolved().scale, minTouch - 0.01f) << name << " is a touch target on the smallest window";
        }
        layout.Invalidate();
    }

    TEST_F(UISettingsScreenTest, EveryWordIsAskedInEveryLanguageNotOnlyTheLongestString) {
        // The same walk as Problems(), written out: each control with text, with the width it got on the narrowest window, in each language
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        UICanvasLayout& layout = _panel->GetLayout();
        size_t languages = 0;
        ForEachLanguage([&](PoolString language) {
            ++languages;
            LayOutInTheNarrowestWindow(layout);
            const float scale = layout.GetResolved().scale;
            for (const char* name : kTextControlNames) {
                const UIElement* element = FindElement(name);
                ASSERT_NE(element, nullptr) << name;
                const float air = IsButton(*element) ? kButtonAir : 0.0f;
                const UIContentFit fit = element->CheckContent(element->rect.w - 2.0f * air, element->rect.h, scale);
                EXPECT_TRUE(fit.hasContent) << language.ToStringView().data() << " " << name;
                EXPECT_TRUE(fit.fits) << language.ToStringView().data() << " " << name << " '" << ShownText(*element).c_str() << "' in " << element->rect.w << " units";
            }
        });
        EXPECT_EQ(languages, 3u) << "en, ru and de";
        layout.Invalidate();
    }

    TEST_F(UISettingsScreenTest, TheLongLanguageIsAtLeastFortyPercentLongerThanEnglish) {
        // The test language only means something if it is long: the captions together, in characters, not bytes
        UIStrings& strings = CoreManager::GetUIStrings();
        size_t english = 0;
        size_t german = 0;
        for (const char* key : kLanguageKeys) {
            ASSERT_TRUE(strings.SetLanguage("en"_intern));
            english += Characters(Require(strings.Find(key)));
            ASSERT_TRUE(strings.SetLanguage("de"_intern));
            german += Characters(Require(strings.Find(key)));
        }
        strings.SetLanguage("en"_intern);
        EXPECT_GE(static_cast<float>(german), kLongerShare * static_cast<float>(english)) << german << " characters against " << english;
    }

    // ---- the shape: caption beside the control, or above it ---------------------------------------------------------------

    TEST_F(UISettingsScreenTest, InALandscapeViewEveryRowIsOneLineHighAndTheCaptionIsBesideItsControl) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        Draw();
        const UIElement& plate = Get<UIStack>("Plate");
        EXPECT_LE(plate.rect.w, 640.01f);
        EXPECT_NEAR(plate.rect.h, 416.0f, 0.01f);

        for (const char* row : {"Sound", "MusicRow", "DownloadRow", "LanguageRow", "Ok"}) {
            const UIElement* element = FindElement(row);
            ASSERT_NE(element, nullptr) << row;
            EXPECT_GE(element->rect.h, kScreenControlHeight - 0.01f) << row;
        }
        const struct {
            const char* caption;
            const char* control;
        } pairs[] = {{"MusicCaption", "Volume"}, {"DownloadCaption", "Progress"}, {"LanguageCaption", "Language"}};
        for (const auto& pair : pairs) {
            const UIElement* caption = FindElement(pair.caption);
            const UIElement* control = FindElement(pair.control);
            ASSERT_NE(control, nullptr);
            ASSERT_TRUE(caption && control) << pair.caption;
            EXPECT_LT(caption->rect.x + caption->rect.w, control->rect.x + 0.01f) << pair.caption;
            EXPECT_GE(control->rect.w, 200.0f) << pair.control << " keeps room to be used";
        }
        EXPECT_NEAR(El("Volume").rect.h, kScreenControlHeight, 0.01f) << "the thumb is the height of the slider: a touch target";
        const UIElement& bar = Get<UIProgressBar>("Progress");
        const UIElement& row = Get<UIStack>("DownloadRow");
        EXPECT_NEAR(bar.rect.y + bar.rect.h * 0.5f, row.rect.y + row.rect.h * 0.5f, 0.01f) << "the thin bar is centered in its row";
        EXPECT_LT(bar.rect.x + bar.rect.w, El("Percent").rect.x + 0.01f);
    }

    TEST_F(UISettingsScreenTest, InAPortraitViewTheLanguageCaptionIsAboveItsGroupAndTheSegmentsKeepTheirHeight) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kPortraitWidth, kPortraitHeight));
        Draw();
        const UIElement& caption = El("LanguageCaption");
        const UIElement& group = Get<UISegmented>("Language");
        EXPECT_LT(caption.rect.y + caption.rect.h, group.rect.y + 0.01f);
        EXPECT_NEAR(caption.rect.x, group.rect.x, 0.01f);
        EXPECT_NEAR(group.rect.h, kScreenControlHeight, 0.01f);
        const UIElement& plate = Get<UIStack>("Plate");
        EXPECT_NEAR(plate.rect.h, 456.0f, 0.01f);
        EXPECT_GE(plate.rect.y, 0.0f);
        EXPECT_LE(plate.rect.y + plate.rect.h, kPortraitHeight);
        EXPECT_GE(El("Volume").rect.h, kScreenControlHeight - 0.01f);
    }

    // ---- hidden, shown (modal) ---------------------------------------------------------------------------------------------

    TEST_F(UISettingsScreenTest, ThePanelHasAHiddenModalCanvasAtFirstAndDrawsNothingUntilShown) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        EXPECT_FALSE(_panel->_visible);
        EXPECT_TRUE(_panel->_modal);
        EXPECT_FALSE(_hud->_modal);
        Draw();
        const auto plateDrawn = [this] {
            return std::any_of(_renderer->calls.begin(), _renderer->calls.end(), [](const DrawCall& call) { return call.kind == DrawKind::Rect && call.color.r == 36 && call.color.g == 40; });
        };
        EXPECT_FALSE(plateDrawn());
        ASSERT_NO_FATAL_FAILURE(Show());
        EXPECT_TRUE(plateDrawn());
    }

    TEST_F(UISettingsScreenTest, WhileShownThePanelTakesEveryPressAndTheBoardSeesNoneWhileHiddenItIsTheOtherWayRound) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        BoardClicks board;
        const UIElement& plate = Get<UIStack>("Plate");
        const Pair<float, float> points[] = {CenterOf(El("Sound")), {plate.rect.x + 4.0f, plate.rect.y + 4.0f}, {5.0f, 5.0f}, {kViewWidth - 5.0f, kViewHeight - 5.0f}};
        for (const auto& [x, y] : points) {
            const auto [pressed, released] = ClickAt(x, y);
            EXPECT_TRUE(pressed) << x << "," << y;
            EXPECT_TRUE(released) << x << "," << y;
        }
        EXPECT_EQ(board.downs, 0) << "a modal canvas keeps the press from the board";
        EXPECT_EQ(_game->Total(), 1u) << "only the click on the toggle is an action";

        const auto [x, y] = CenterOf(El("Volume"));
        UICanvasComponent::SetVisibleByName(GetScene(), "Settings"_intern, false);
        Draw();
        const auto [pressed, released] = ClickAt(x, y);
        EXPECT_FALSE(pressed);
        EXPECT_FALSE(released);
        EXPECT_EQ(board.downs, 1);
    }

    TEST_F(UISettingsScreenTest, TheOpenButtonShowsThePanelAndOkHidesIt) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        Draw();
        const UIElement* open = _hud->GetRoot().FindByName("Open"_intern);
        ASSERT_NE(open, nullptr);
        ASSERT_FALSE(open->rect.IsEmpty());
        EXPECT_EQ(ShownText(*open), "Settings");

        ClickAt(CenterOf(*open).first, CenterOf(*open).second);
        EXPECT_EQ(_game->LastAction(), "SettingsOpen"_intern);
        EXPECT_TRUE(_panel->_visible);
        Draw();
        Click(El("Ok"));
        EXPECT_EQ(_game->LastAction(), "SettingsClose"_intern);
        EXPECT_FALSE(_panel->_visible);
    }

    // ---- the controls: toggle, slider, progress bar, language -------------------------------------------------------------

    TEST_F(UISettingsScreenTest, TheToggleAsksForTheOppositeValueAndTheGameMovesTheSwitch) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        const UIElement& soundRow = El("Sound");
        const UIToggleComponent& sound = Toggle("Sound");
        ASSERT_TRUE(sound.IsOn());

        const auto [pressed, released] = Click(soundRow);
        EXPECT_TRUE(pressed && released);
        EXPECT_EQ(_game->LastAction(), "SettingsSound"_intern);
        EXPECT_EQ(_game->LastValue(), "false"_intern) << "the toggle is on, so it asks for off";
        Draw();
        EXPECT_FALSE(sound.IsOn()) << "the game answered with the variable";

        Click(soundRow);
        EXPECT_EQ(_game->LastValue(), "true"_intern);
        Draw();
        EXPECT_TRUE(sound.IsOn());
    }

    TEST_F(UISettingsScreenTest, AToggleClickChangesNothingByItselfWhenTheGameDoesNotAnswer) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        _game.reset();
        Click(El("Sound"));
        Draw();
        EXPECT_TRUE(Toggle("Sound").IsOn());
        EXPECT_EQ(CoreManager::GetUIVars().Get("soundOn"_intern), "true");
    }

    TEST_F(UISettingsScreenTest, TheThumbOfTheToggleStandsAtTheRightWhenOnAndAtTheLeftWhenOff) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        const UIElement& sound = El("Sound");
        const UIElement& caption = El("SoundCaption");
        const UISwitchComponent::Parts parts = UISwitchComponent::GetParts(El("SoundSwitch").rect);
        EXPECT_GT(parts.thumbOn.x, parts.thumbOff.x);
        EXPECT_GE(parts.track.x, caption.rect.x + caption.rect.w - 0.01f) << "the caption never runs under the switch";
        EXPECT_LE(parts.track.x + parts.track.w, sound.rect.x + sound.rect.w + 0.01f);

        // The thumb is drawn: a square of the thumb's size at the place of the state, in a draw call of the renderer (rects are in output pixels = view units here)
        const auto thumbDrawnAt = [&](const Rect& thumb) {
            return std::any_of(_renderer->calls.begin(), _renderer->calls.end(), [&](const DrawCall& call) {
                return call.kind == DrawKind::Rect && std::abs(call.rect.x - thumb.x) < 0.5f && std::abs(call.rect.y - thumb.y) < 0.5f && std::abs(call.rect.w - thumb.w) < 0.5f;
            });
        };
        EXPECT_TRUE(thumbDrawnAt(parts.thumbOn)) << "on";
        CoreManager::GetUIVars().Set("soundOn"_intern, "false");
        Draw();
        EXPECT_TRUE(thumbDrawnAt(parts.thumbOff)) << "off";
    }

    TEST_F(UISettingsScreenTest, ADragOnTheSliderThroughTheInputMovesTheValueOneEmissionPerChange) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        const UISliderComponent& volume = Slider("Volume");
        EXPECT_NEAR(volume.GetFraction(), 0.5f, 1e-4f);
        const float y = VolumeY();

        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(XAt(0.3f), y, true)));
        for (const float fraction : {0.4f, 0.5f, 0.6f, 0.7f}) {
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(XAt(fraction), y, SDL_BUTTON_LMASK));
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(XAt(fraction) + 1.0f, y, SDL_BUTTON_LMASK));  // inside one step: silent
        }
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(XAt(0.7f), y, false)));

        const Vector<String> values = _game->ValuesOf("SettingsVolume");
        const char* const expected[] = {"0.3", "0.4", "0.5", "0.6", "0.7"};
        ASSERT_EQ(values.size(), 5u);
        for (size_t i = 0; i < 5; ++i) {
            EXPECT_EQ(values[i], expected[i]) << i;
        }
        EXPECT_EQ(_game->Total(), 5u) << "the release adds nothing";
        Draw();
        EXPECT_NEAR(volume.GetFraction(), 0.7f, 1e-4f);
        EXPECT_EQ(CoreManager::GetUIVars().Get("volume"_intern), "0.7");
    }

    TEST_F(UISettingsScreenTest, ADragThatLeavesTheSliderKeepsFollowingAndClampsAtTheEnds) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        const float y = VolumeY();
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(XAt(0.5f), y, true)));
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(XAt(1.0f) + 300.0f, y - 200.0f, SDL_BUTTON_LMASK));
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(-100.0f, y + 300.0f, SDL_BUTTON_LMASK));
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(-100.0f, y + 300.0f, false));

        const Vector<String> values = _game->ValuesOf("SettingsVolume");
        ASSERT_EQ(values.size(), 3u);
        EXPECT_EQ(values[0], "0.5");
        EXPECT_EQ(values[1], "1") << "past the end: the maximum, written without a decimal point";
        EXPECT_EQ(values[2], "0");
        Draw();
        const UIElement& volume = El("Volume");
        const UISliderComponent::Parts parts = UISliderComponent::GetParts(volume.rect, Slider("Volume").GetFraction());
        EXPECT_GE(parts.thumb.x, volume.rect.x - 0.01f);
        EXPECT_LE(parts.thumb.x + parts.thumb.w, volume.rect.x + volume.rect.w + 0.01f);
    }

    TEST_F(UISettingsScreenTest, AGameThatDoesNotAnswerLeavesTheThumbWhereItWas) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        _game.reset();
        const float y = VolumeY();
        CoreManager::GetUIInput().DispatchMouseButtonDown(Button(XAt(0.9f), y, true));
        CoreManager::GetUIInput().DispatchMouseButtonUp(Button(XAt(0.9f), y, false));
        Draw();
        EXPECT_NEAR(Slider("Volume").GetFraction(), 0.5f, 1e-4f);
    }

    TEST_F(UISettingsScreenTest, TheProgressBarFollowsItsVariableAndTakesNoClick) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        const UIProgressBar& bar = Get<UIProgressBar>("Progress");
        EXPECT_NEAR(bar.GetFraction(), 0.4f, 1e-4f);
        EXPECT_EQ(ShownText(El("Percent")), "40%");

        for (const char* value : {"0", "75", "100", "250", "abc"}) {
            CoreManager::GetUIVars().Set("progress"_intern, value);
            Draw();
            const float expected = String(value) == "75" ? 0.75f : (String(value) == "100" || String(value) == "250") ? 1.0f : 0.0f;
            EXPECT_NEAR(bar.GetFraction(), expected, 1e-4f) << value;
            EXPECT_EQ(ShownText(El("Percent")), Format("{}%", value).c_str()) << value;
        }
        const size_t before = _game->Total();
        Click(bar);
        EXPECT_EQ(_game->Total(), before) << "a bar is not a button";
    }

    TEST_F(UISettingsScreenTest, TheLanguageSegmentsEmitTheirValueAndTheGameSwitchesEveryCaptionAtTheNextDraw) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        EXPECT_TRUE(El("LanguageEn").selected);
        EXPECT_EQ(ShownText(El("Title")), "Settings");

        for (const Language& language : kLanguages) {
            Draw();  // the layout of the narrowest window below is not the one the clicks are aimed at
            Click(El(language.segment));
            EXPECT_EQ(_game->LastAction(), "SettingsLanguage"_intern) << language.id;
            EXPECT_EQ(_game->LastValue(), PoolString::Intern(language.id));
            Draw();
            EXPECT_EQ(CoreManager::GetUIStrings().GetLanguage(), PoolString::Intern(language.id));
            EXPECT_EQ(ShownText(El("Title")), language.title) << "the title follows the language at the next draw";
            EXPECT_EQ(ShownText(Require(_hud->GetRoot().FindByName("Open"_intern))), Require(CoreManager::GetUIStrings().Find("settings.open"))) << "the button of the other canvas follows too";
            EXPECT_TRUE(El(language.segment).selected);
            EXPECT_EQ(Get<UISegmented>("Language").GetSelectedValue(), language.id);
            EXPECT_EQ(ShownText(El("LanguageDe")), "Deutsch");
            UICanvasLayout& layout = _panel->GetLayout();
            LayOutInTheNarrowestWindow(layout);
            EXPECT_TRUE(Problems(layout).empty()) << language.id << ": " << Problems(layout).c_str();
            layout.Invalidate();
        }
    }

    TEST_F(UISettingsScreenTest, AllWordsOfTheScreenChangeWithTheLanguageAndOnlyTheNamesOfTheLanguagesStay) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        const char* const names[] = {"Title", "SoundCaption", "MusicCaption", "DownloadCaption", "LanguageCaption", "Ok"};
        Vector<String> english;
        for (const char* name : names) {
            english.push_back(ShownText(El(name)));
        }
        for (const char* id : {"ru", "de"}) {
            ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage(PoolString::Intern(id)));
            Draw();
            for (size_t i = 0; i < english.size(); ++i) {
                const String shown = ShownText(El(names[i]));
                EXPECT_NE(shown, english[i]) << id << " " << names[i];
                EXPECT_FALSE(shown.empty()) << id << " " << names[i];
            }
        }
    }

    // ---- a missing translation ---------------------------------------------------------------------------------------------

    TEST_F(UISettingsScreenTest, AKeyMissingInTheLongLanguageFallsBackToEnglishAndStillFits) {
        String table = kSettingsStringsXml;
        const size_t at = table.find(R"(<string key="settings.ok" text="Übernehmen"/>)");
        ASSERT_NE(at, String::npos);
        table.erase(at, String(R"(<string key="settings.ok" text="Übernehmen"/>)").size());
        ASSERT_NO_FATAL_FAILURE(LoadStrings(table, kLanguageCount));
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("de"_intern));
        Draw();
        EXPECT_EQ(ShownText(El("Ok")), "OK") << "the default language answers";
        EXPECT_TRUE(CoreManager::GetUIStrings().GetMissing().empty()) << "a fallback is not a missing key";
        UICanvasLayout& layout = _panel->GetLayout();
        LayOutInTheNarrowestWindow(layout);
        EXPECT_TRUE(Problems(layout).empty()) << Problems(layout).c_str();
        layout.Invalidate();
    }

    // ---- a deliberately broken copy must be caught ------------------------------------------------------------------------

    TEST_F(UISettingsScreenTest, ASliderUnderTheTouchSizeIsCaughtByTheValidator) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](String& xml) { Replace(xml, R"(name="Volume" flex="1" height="56")", R"(name="Volume" flex="1" height="30")"); }));
        EXPECT_NEAR(El("Volume").rect.h, 30.0f, 0.01f);
        const auto failures = ValidatePanel(*_panel);
        EXPECT_TRUE(HasIssue(failures, UILayoutIssueKind::TouchTargetTooSmall)) << DescribeMatrixFailures(failures).c_str();
        EXPECT_NE(ProblemsOnMatrix(*_panel).find("Volume is 30.0 units high"), String::npos);
    }

    TEST_F(UISettingsScreenTest, ASliderWithoutAnExplicitHeightHasNoRectAndIsCaughtHere) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](String& xml) { Replace(xml, R"(name="Volume" flex="1" height="56")", R"(name="Volume" flex="1")"); }));
        ASSERT_NO_FATAL_FAILURE(Show());
        EXPECT_NEAR(El("Volume").rect.h, 0.0f, 0.01f);
        UICanvasLayout& layout = _panel->GetLayout();
        const String problems = Problems(layout);
        EXPECT_NE(problems.find("Volume '' has an empty rect"), String::npos) << problems.c_str();
    }

    TEST_F(UISettingsScreenTest, AToggleCaptionTooLongForItsAreaIsCaughtByTheFitCheckAndNotByTheValidator) {
        const auto lengthen = [](String& xml) { Replace(xml, R"(text="{#settings.sound}")", R"(text="Sound effects, music and every other noise of the game")"); };
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, lengthen));
        EXPECT_FALSE(HasIssue(ValidatePanel(*_panel), UILayoutIssueKind::TextDoesNotFit)) << "an ellipsis label can shrink: the validator does not report it";
        const String matrix = ProblemsOnMatrix(*_panel);
        EXPECT_NE(matrix.find("SoundCaption cuts 'Sound effects"), String::npos) << matrix.c_str();

        UICanvasLayout& layout = _panel->GetLayout();
        LayOutInTheNarrowestWindow(layout);
        EXPECT_NE(Problems(layout).find("SoundCaption cuts 'Sound effects"), String::npos);
        layout.Invalidate();
    }

    TEST_F(UISettingsScreenTest, AWordTooLongInTheLongLanguageIsCaughtInTheNarrowestWindowAlone) {
        // A German caption made too long for the area left of the switch; the narrowest window (about 689 units of safe width) finds it, and the failure names the language
        String table = kSettingsStringsXml;
        const String original = R"(text="Geräuscheffekte")";
        const size_t at = table.find(original);
        ASSERT_NE(at, String::npos);
        table.replace(at, original.size(), R"(text="Geräuscheffekte und Klangeinstellungen des Spiels")");
        ASSERT_NO_FATAL_FAILURE(LoadStrings(table, kLanguageCount));
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        const String problems = ProblemsInTheNarrowestWindow(*_panel);
        EXPECT_NE(problems.find("[de] SoundCaption cuts 'Geräuscheffekte und"), String::npos) << problems.c_str();
        EXPECT_EQ(problems.find("[en]"), String::npos) << "English and Russian are fine: the failure names its language";
        EXPECT_EQ(problems.find("[ru]"), String::npos);
    }

    TEST_F(UISettingsScreenTest, AKeyMissingEverywhereShowsTheKeyInTheLongLanguageAndTheFitCheckStillMeasuresIt) {
        ASSERT_NO_FATAL_FAILURE(
            LoadScene(kViewWidth, kViewHeight, [](String& xml) { Replace(xml, R"(text="{#settings.sound}")", R"(text="{#settings.sound.effects.volume.and.noise.of.the.game.and.of.the.menu}")"); }));
        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("de"_intern));
        Draw();
        EXPECT_EQ(ShownText(El("SoundCaption")), "settings.sound.effects.volume.and.noise.of.the.game.and.of.the.menu") << "the key itself, without the braces: never an empty label";
        ASSERT_EQ(CoreManager::GetUIStrings().GetMissing().size(), 1u);
        EXPECT_EQ(CoreManager::GetUIStrings().GetMissing()[0].language, "de"_intern);

        UICanvasLayout& layout = _panel->GetLayout();
        LayOutInTheNarrowestWindow(layout);
        const String problems = Problems(layout);
        EXPECT_NE(problems.find("SoundCaption cuts 'settings.sound.effects.volume.and.noise.of.the.game.and.of.the.menu'"), String::npos) << problems.c_str();
        layout.Invalidate();
    }

}  // namespace BECore::Tests
