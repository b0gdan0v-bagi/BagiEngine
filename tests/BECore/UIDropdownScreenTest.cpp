#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UILabel.h>
#include <BECore/UI/UIStack.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UITextComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UILayoutMatrix.h>
#include <CoreSDL/SDLEvents.h>
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
        constexpr float kRowHeight = 56.0f;  // a touch target at the smallest fit scale of the matrix
        constexpr float kButtonAir = 8.0f;   // the text of a button keeps this much from each edge of its fill
        constexpr size_t kOptionCount = 5;

        // Plate: 16 + 44 (title) + 12 + 56 (the dropdown) + 12 + 56 (OK) + 16 = 212, taller by 8 for the rounding of a stack: 220. The options have no height of their own: `optionHeight`.
        constexpr const char* kLevelsXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main" viewWidth="@VIEW_W@" viewHeight="@VIEW_H@"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="SceneView" useSafeArea="true">
        <elements>
          <element type="UIButton" name="Open" anchor="TopRight" x="-12" y="12" width="190" height="56" text="{#levels.open}" fontSize="24" action="LevelsOpen" color="45,75,120,255"/>
        </elements>
      </component>
    </components></node>
    <node name="Levels" layer="20"><components>
      <component type="UICanvasComponent" space="SceneView" useSafeArea="true" modal="true" visible="false">
        <elements>
          <element type="UIElement" name="Dim" anchor="Stretch" ignoreSafeArea="true" color="0,0,0,150" blocksInput="true"/>
          <element type="UIStack" name="Plate" anchor="Center" width="94%" maxWidth="640" height="220" direction="Vertical" spacing="12" padding="16" crossAlign="Stretch" color="36,40,52,255" blocksInput="true">
            <children>
              <element type="UILabel" name="Title" height="44" text="{#levels.title}" fontSize="30" align="Center"/>
              <element type="UIDropdown" name="Level" bind="level" action="SetLevel" height="56" optionHeight="56" text="{#levels.pick}" fontSize="24" verticalAlign="Center" color="70,70,100,255">
                <children>
                  <element type="UIButton" name="LevelNovice" text="{#level.novice}" fontSize="24" value="novice" color="70,70,100,255"/>
                  <element type="UIButton" name="LevelEasy" text="{#level.easy}" fontSize="24" value="easy" color="70,70,100,255"/>
                  <element type="UIButton" name="LevelNormal" text="{#level.normal}" fontSize="24" value="normal" color="70,70,100,255"/>
                  <element type="UIButton" name="LevelHard" text="{#level.hard}" fontSize="24" value="hard" color="70,70,100,255"/>
                  <element type="UIButton" name="LevelMaster" text="{#level.master}" fontSize="24" value="master" color="70,70,100,255"/>
                </children>
              </element>
              <element type="UIButton" name="Ok" height="56" text="{#levels.ok}" fontSize="26" action="LevelsClose" color="40,90,60,255"/>
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

        std::string SceneXml(float viewWidth, float viewHeight, const std::function<void(std::string&)>& edit = {}) {
            std::string xml = kLevelsXml;
            const auto replace = [&xml](const std::string& what, const std::string& with) {
                for (size_t at = xml.find(what); at != std::string::npos; at = xml.find(what, at + with.size())) {
                    xml.replace(at, what.size(), with);
                }
            };
            replace("@VIEW_W@", std::to_string(static_cast<int>(viewWidth)));
            replace("@VIEW_H@", std::to_string(static_cast<int>(viewHeight)));
            if (edit) {
                edit(xml);
            }
            return xml;
        }

        /// Replaces the first @p what in @p xml, and fails the test when it is not there (a broken copy that changed nothing would prove nothing)
        void Replace(std::string& xml, const std::string& what, const std::string& with) {
            const size_t at = xml.find(what);
            ASSERT_NE(at, std::string::npos) << what;
            xml.replace(at, what.size(), with);
        }

        /// The five options as composed elements: a caption on the element and a clicker with the value (an option has no action), the second one icon-left (an image child and a caption child)
        void ComposeOptions(std::string& xml) {
            constexpr const char* kValues[] = {"novice", "easy", "normal", "hard", "master"};
            for (size_t i = 0; i < kOptionCount; ++i) {
                const std::string button = std::string("<element type=\"UIButton\" name=\"") + kOptionNames[i] + "\" text=\"{#" + kOptionKeys[i] + "}\" fontSize=\"24\" value=\"" + kValues[i] +
                                           "\" color=\"70,70,100,255\"/>";
                const std::string caption =
                    std::string("<component type=\"UITextComponent\" text=\"{#") + kOptionKeys[i] + "}\" align=\"Center\" verticalAlign=\"Center\" overflow=\"Ellipsis\" wrap=\"false\"/>";
                const std::string clicker = std::string("<component type=\"UIInputClicker\" value=\"") + kValues[i] + "\"/>";
                std::string composed;
                if (i == 1) {
                    composed = std::string("<element type=\"UIElement\" name=\"") + kOptionNames[i] +
                               "\" fontSize=\"24\" color=\"70,70,100,255\" layout=\"Stack\" direction=\"Horizontal\" padding=\"8,0,0,0\" spacing=\"6\" crossAlign=\"Center\">" + "<components>" +
                               clicker + "</components><children>" +
                               "<element type=\"UIElement\" name=\"LevelEasyIcon\" height=\"50%\" aspectRatio=\"1\"><components><component type=\"UIImageComponent\"/></components></element>" +
                               "<element type=\"UIElement\" name=\"LevelEasyCaption\" flex=\"1\" height=\"100%\"><components>" + caption + "</components></element></children></element>";
                } else {
                    composed = std::string("<element type=\"UIElement\" name=\"") + kOptionNames[i] + "\" fontSize=\"24\" color=\"70,70,100,255\"><components>" + caption + clicker +
                               "</components></element>";
                }
                Replace(xml, button, composed);
            }
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

        size_t Characters(eastl::string_view text) {
            size_t count = 0;
            for (const char c : text) {
                if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) {
                    ++count;
                }
            }
            return count;
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
            eastl::vector<PoolString> _actions;
            eastl::vector<PoolString> _values;
        };

        /// Every control of the tree that shows text or takes a press, depth first. The options of a closed list have no rect and are not looked at
        void CollectControls(const UIElement& element, eastl::vector<const UIElement*>& found) {
            if (element.Cast<UILabel>() || element.Cast<UIButton>() || element.Cast<UIDropdown>() || element.GetComponent<UITextComponent>()) {
                found.push_back(&element);
            }
            if (element.HasPopupChildren() && !element.IsPopupOpen()) {
                return;
            }
            for (const auto& child : element.children) {
                CollectControls(*child, found);
            }
        }

        eastl::string ShownText(const UIElement& element) {
            if (const UITextComponent* caption = element.GetComponent<UITextComponent>()) {
                return eastl::string(caption->GetDisplayText());
            }
            const UILabel* label = element.Cast<UILabel>();
            if (!label) {
                label = element.Cast<UIButton>();
            }
            if (!label) {
                label = element.Cast<UIDropdown>();  // Cast is of the exact class
            }
            return label ? eastl::string(label->GetDisplayText()) : eastl::string();
        }

        /// What is wrong with a laid-out panel in the language that is set now, one problem to a line, nothing when it is right: a control with no rect, text that its control cuts
        /// (the header against the area left of its arrow), and a control that takes a press and is under kRowHeight
        eastl::string Problems(const UICanvasLayout& layout) {
            eastl::string problems;
            const float scale = layout.GetResolved().scale;
            eastl::vector<const UIElement*> controls;
            CollectControls(*layout.root, controls);
            for (const UIElement* control : controls) {
                if (control->rect.IsEmpty()) {
                    if (control->active.visible) {
                        problems += Format("{} '{}' has an empty rect (a missing height?)\n", control->name.ToStringView(), ShownText(*control).c_str());
                    }
                    continue;
                }
                const float air = control->Cast<UIButton>() ? kButtonAir : 0.0f;
                const UIContentFit fit = control->CheckContent(control->rect.w - 2.0f * air, control->rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    problems += Format("{} cuts '{}': {:.1f} units wide with {:.1f} of air each side, at scale {:.3f}\n", control->name.ToStringView(), ShownText(*control).c_str(), control->rect.w,
                                       air, scale);
                }
                if (control->IsInteractive() && control->rect.h < kRowHeight - 0.01f) {
                    problems += Format("{} is {:.1f} units high, under {:.0f}\n", control->name.ToStringView(), control->rect.h, kRowHeight);
                }
            }
            return problems;
        }

        /// Runs @p check once for every language of the table and puts the language that was current back
        template <typename Check>
        void ForEachLanguage(Check&& check) {
            UIStrings& strings = CoreManager::GetUIStrings();
            const PoolString saved = strings.GetLanguage();
            const eastl::vector<PoolString> languages = strings.GetLanguages();
            for (const PoolString language : languages) {
                ASSERT_TRUE(strings.SetLanguage(language)) << language.ToStringView().data();
                check(language);
            }
            strings.SetLanguage(saved);
        }

    }  // namespace

    class UIDropdownScreenTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetFontAtlasLibrary().Clear();
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);
            LoadStrings(kStringsXml);
            CoreManager::GetUIVars().Set("level"_intern, "master");
        }

        void TearDown() override {
            _game.reset();
            _hud.Reset();
            _levels.Reset();
            _manager.Reset();
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        /// The table of the screen, through the reader of the file; the first language is the default one and the current one
        static void LoadStrings(const std::string& xml) {
            UIStrings& strings = CoreManager::GetUIStrings();
            strings.Clear();
            auto config = XmlConfig::Create();
            ASSERT_TRUE(config.LoadFromString(eastl::string_view(xml.data(), xml.size())));
            const eastl::vector<eastl::string> problems = strings.LoadFromXml(config.GetRoot());
            EXPECT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0].c_str());
            ASSERT_EQ(strings.GetLanguages().size(), 3u);
        }

        void LoadScene(float viewWidth = kViewWidth, float viewHeight = kViewHeight, const std::function<void(std::string&)>& edit = {}) {
            _game.reset();
            _hud.Reset();
            _levels.Reset();
            XmlDeserializer deserializer;
            const std::string xml = SceneXml(viewWidth, viewHeight, edit);
            ASSERT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _renderer->width = viewWidth;
            _renderer->height = viewHeight;

            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == "Hud"_intern) {
                    _hud = node->GetComponent<UICanvasComponent>();
                } else if (node->GetName() == "Levels"_intern) {
                    _levels = node->GetComponent<UICanvasComponent>();
                }
            }
            ASSERT_TRUE(_hud);
            ASSERT_TRUE(_levels);
            _hud->UpdateLayout(1280.0f, 720.0f);
            _levels->UpdateLayout(1280.0f, 720.0f);
            _game.emplace(GetScene());
        }

        Scene& GetScene() const {
            return *_manager->GetSceneByName("Main"_intern);
        }

        void Show() {
            ASSERT_TRUE(UICanvasComponent::SetVisibleByName(GetScene(), "Levels"_intern, true));
            Draw();
        }

        void Draw() {
            _manager->SetActiveScene("Main"_intern);
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        UILayoutContext ViewContext() const {
            UILayoutContext context;
            context.sceneViewWidth = GetScene().GetViewWidth();
            context.sceneViewHeight = GetScene().GetViewHeight();
            return context;
        }

        template <typename T>
        T& Get(const char* name) const {
            UIElement* element = _levels->GetRoot().FindByName(PoolString::Intern(name));
            T* typed = element ? element->Cast<T>() : nullptr;
            if (typed == nullptr) {
                ADD_FAILURE() << name << (element ? " is not the expected control" : " is not in the panel");
                std::abort();
            }
            return *typed;
        }

        /// Any element of the panel by name, whatever its class (`Get<T>` wants the exact type)
        UIElement& Find(const char* name) const {
            UIElement* element = _levels->GetRoot().FindByName(PoolString::Intern(name));
            if (element == nullptr) {
                ADD_FAILURE() << name << " is not in the panel";
                std::abort();
            }
            return *element;
        }

        static std::pair<float, float> CenterOf(const UIElement& element) {
            return {element.rect.x + element.rect.w * 0.5f, element.rect.y + element.rect.h * 0.5f};
        }

        bool Click(const UIElement& element) {
            const auto [x, y] = CenterOf(element);
            const bool pressed = CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));
            const bool released = CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
            return pressed && released;
        }

        /// The panel shown, the list opened by a click on the header, and drawn: the state the rest of the test looks at
        void ShowAndOpen() {
            ASSERT_NO_FATAL_FAILURE(Show());
            ASSERT_TRUE(Click(Get<UIDropdown>("Level")));
            Draw();
            ASSERT_TRUE(Get<UIDropdown>("Level").IsOpen());
            ASSERT_FALSE(Find("LevelMaster").rect.IsEmpty());
        }

        /// The panel on the whole matrix once for every language: the findings of the validator, each case named with its language
        eastl::vector<UIMatrixFailure> ValidatePanel(UICanvasComponent& canvas) const {
            UILayoutValidatorOptions options;
            options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
            eastl::vector<UIMatrixFailure> all;
            ForEachLanguage([&](PoolString language) {
                for (UIMatrixFailure& failure : ValidateOnMatrix(canvas.GetLayout(), options, ViewContext())) {
                    failure.label = Format("[{}] {}", language.ToStringView(), failure.label.c_str());
                    all.push_back(std::move(failure));
                }
            });
            return all;
        }

        /// Problems() on every case of the matrix, in every language: the first lines of the first cases that have any
        eastl::string ProblemsOnMatrix(UICanvasComponent& canvas) const {
            constexpr size_t kReported = 4;
            eastl::string report;
            size_t reported = 0;
            UICanvasLayout& layout = canvas.GetLayout();
            ForEachLanguage([&](PoolString language) {
                for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
                    UILayoutContext context = ViewContext();
                    context.outputWidth = matrixCase.context.outputWidth;
                    context.outputHeight = matrixCase.context.outputHeight;
                    context.safeArea = matrixCase.context.safeArea;
                    layout.Update(context);
                    const eastl::string problems = Problems(layout);
                    if (!problems.empty() && reported++ < kReported) {
                        report += Format("[{}] {}:\n{}", language.ToStringView(), matrixCase.label.c_str(), problems.c_str());
                    }
                }
            });
            layout.Invalidate();
            return report;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _hud;
        IntrusivePtr<UICanvasComponent> _levels;
        std::optional<LevelsGame> _game;
    };

    // ---- the size matrix, once per language, closed and open ------------------------------------------------------------

    TEST_F(UIDropdownScreenTest, TheClosedPanelPassesTheSizeMatrixInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        const auto failures = ValidatePanel(*_levels);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        const auto hudFailures = ValidatePanel(*_hud);
        EXPECT_TRUE(hudFailures.empty()) << DescribeMatrixFailures(hudFailures).c_str();
    }

    TEST_F(UIDropdownScreenTest, TheOpenPanelPassesTheSizeMatrixInEveryLanguageInALandscapeAndAPortraitView) {
        for (const auto [width, height] : {std::pair{kViewWidth, kViewHeight}, std::pair{kPortraitWidth, kPortraitHeight}}) {
            ASSERT_NO_FATAL_FAILURE(LoadScene(width, height));
            ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
            const auto failures = ValidatePanel(*_levels);
            EXPECT_TRUE(failures.empty()) << width << " x " << height << ": " << DescribeMatrixFailures(failures).c_str();
        }
    }

    TEST_F(UIDropdownScreenTest, NoWordIsCutClosedOrOpenOnAnyCaseOfTheMatrixInAnyLanguage) {
        for (const auto [width, height] : {std::pair{kViewWidth, kViewHeight}, std::pair{kPortraitWidth, kPortraitHeight}}) {
            ASSERT_NO_FATAL_FAILURE(LoadScene(width, height));
            ASSERT_NO_FATAL_FAILURE(Show());
            const eastl::string closed = ProblemsOnMatrix(*_levels);
            EXPECT_TRUE(closed.empty()) << width << " closed: " << closed.c_str();

            ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
            const eastl::string open = ProblemsOnMatrix(*_levels);
            EXPECT_TRUE(open.empty()) << width << " open: " << open.c_str();
        }
    }

    TEST_F(UIDropdownScreenTest, EveryOptionAndTheHeaderAreAskedInEveryLanguage) {
        // The same walk as Problems(), written out for the open list: the header and the five options, each with the width it got
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        UICanvasLayout& layout = _levels->GetLayout();
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
                const UIButton& option = Get<UIButton>(name);
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
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](std::string& xml) { Replace(xml, "name=\"Plate\" anchor=\"Center\"", "name=\"Plate\" anchor=\"Bottom\""); }));
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        const UIDropdown& header = Get<UIDropdown>("Level");
        for (const char* name : kOptionNames) {
            EXPECT_LE(Get<UIButton>(name).rect.y + Get<UIButton>(name).rect.h, header.rect.y + 0.01f) << name << " is above the header";
            EXPECT_GE(Get<UIButton>(name).rect.y, 0.0f) << name;
        }
        const auto failures = ValidatePanel(*_levels);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(UIDropdownScreenTest, TheListIsUnderTheHeaderAndAboveTheOkButtonItCovers) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        const UIDropdown& header = Get<UIDropdown>("Level");
        EXPECT_NEAR(Get<UIButton>("LevelNovice").rect.y, header.rect.y + header.rect.h, 0.01f);
        EXPECT_GE(Get<UIButton>("LevelMaster").rect.w, header.rect.w - 0.01f);
        EXPECT_GT(Get<UIButton>("LevelMaster").rect.y + Get<UIButton>("LevelMaster").rect.h, Get<UIButton>("Ok").rect.y) << "the list covers the OK button";
        EXPECT_EQ(_levels->GetLayout().HitTest(CenterOf(Get<UIButton>("LevelEasy")).first, CenterOf(Get<UIButton>("LevelEasy")).second), &Get<UIButton>("LevelEasy"));
    }

    // ---- the game's part ----------------------------------------------------------------------------------------------------

    TEST_F(UIDropdownScreenTest, AChoiceIsAnActionTheGameAnswersWithTheVariableAndTheHeaderFollows) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        EXPECT_EQ(Get<UIDropdown>("Level").GetDisplayText(), eastl::string("Grandmaster"));

        ASSERT_TRUE(Click(Get<UIButton>("LevelHard")));
        EXPECT_EQ(_game->CountOf("SetLevel"), 1u);
        EXPECT_EQ(_game->LastValue(), "hard"_intern);
        EXPECT_EQ(_game->CountOf("LevelsClose"), 0u) << "the option took the click, not the OK button under it";
        EXPECT_FALSE(Get<UIDropdown>("Level").IsOpen());

        Draw();
        EXPECT_EQ(Get<UIDropdown>("Level").GetDisplayText(), eastl::string("Hard"));
        EXPECT_TRUE(Get<UIButton>("LevelEasy").rect.IsEmpty()) << "closed again";
    }

    TEST_F(UIDropdownScreenTest, APressOutsideTheListClosesItAndNothingElseHappens) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        const size_t before = _game->Total();
        ASSERT_TRUE(Click(Get<UILabel>("Title")));
        EXPECT_FALSE(Get<UIDropdown>("Level").IsOpen());
        EXPECT_EQ(_game->Total(), before);
    }

    // ---- broken copies: each check must be able to fail ----------------------------------------------------------------------

    TEST_F(UIDropdownScreenTest, AnOptionOfThirtyUnitsIsReportedByTheValidatorAndByProblems) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](std::string& xml) { Replace(xml, "name=\"LevelEasy\"", "name=\"LevelEasy\" height=\"30\""); }));
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        EXPECT_FALSE(ValidatePanel(*_levels).empty());
        EXPECT_NE(Problems(_levels->GetLayout()).find("LevelEasy is 30.0 units high"), eastl::string::npos);
    }

    TEST_F(UIDropdownScreenTest, AWordThatIsTooLongIsReportedByProblemsAndNotByTheValidator) {
        std::string xml = kStringsXml;
        Replace(xml, "text=\"Grandmaster\"", "text=\"Grandmaster of the Eastern Wind Tournament Circuit, the Final Round, with Every Rule Enabled\"");
        ASSERT_NO_FATAL_FAILURE(LoadStrings(xml));
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        const eastl::string problems = ProblemsOnMatrix(*_levels);
        EXPECT_NE(problems.find("cuts 'Grandmaster of"), eastl::string::npos) << problems.c_str();
        EXPECT_TRUE(ValidatePanel(*_levels).empty()) << "an ellipsis label can shrink: only CheckContent sees it";
    }

    TEST_F(UIDropdownScreenTest, AListThatCannotFitIsReportedByTheValidator) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](std::string& xml) { Replace(xml, "optionHeight=\"56\"", "optionHeight=\"200\""); }));
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        const auto failures = ValidatePanel(*_levels);
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
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](std::string& xml) { Replace(xml, "height=\"56\" optionHeight=\"56\"", "optionHeight=\"56\""); }));
        ASSERT_NO_FATAL_FAILURE(Show());
        const eastl::string problems = Problems(_levels->GetLayout());
        EXPECT_NE(problems.find("Level 'Grandmaster' has an empty rect"), eastl::string::npos) << problems.c_str();
    }

    // ---- the options as composed elements: the same panel, the same findings ------------------------------------------------

    TEST_F(UIDropdownScreenTest, ComposedOptionsPassTheSizeMatrixClosedAndOpenInEveryLanguageInBothViews) {
        for (const auto [width, height] : {std::pair{kViewWidth, kViewHeight}, std::pair{kPortraitWidth, kPortraitHeight}}) {
            ASSERT_NO_FATAL_FAILURE(LoadScene(width, height, ComposeOptions));
            ASSERT_NO_FATAL_FAILURE(Show());
            const auto closed = ValidatePanel(*_levels);
            EXPECT_TRUE(closed.empty()) << width << " closed: " << DescribeMatrixFailures(closed).c_str();

            ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
            const auto open = ValidatePanel(*_levels);
            EXPECT_TRUE(open.empty()) << width << " open: " << DescribeMatrixFailures(open).c_str();
        }
    }

    TEST_F(UIDropdownScreenTest, NoWordOfAComposedOptionIsCutOnAnyCaseOfTheMatrixInAnyLanguage) {
        for (const auto [width, height] : {std::pair{kViewWidth, kViewHeight}, std::pair{kPortraitWidth, kPortraitHeight}}) {
            ASSERT_NO_FATAL_FAILURE(LoadScene(width, height, ComposeOptions));
            ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
            const eastl::string open = ProblemsOnMatrix(*_levels);
            EXPECT_TRUE(open.empty()) << width << " open: " << open.c_str();
        }
    }

    TEST_F(UIDropdownScreenTest, TheProblemsCheckSeesAComposedOptionAndItsCaptionChild) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](std::string& xml) {
            ComposeOptions(xml);
            Replace(xml, "name=\"LevelNovice\" fontSize=\"24\"", "name=\"LevelNovice\" height=\"30\" fontSize=\"24\"");
        }));
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        EXPECT_FALSE(ValidatePanel(*_levels).empty());
        EXPECT_NE(Problems(_levels->GetLayout()).find("LevelNovice is 30.0 units high"), eastl::string::npos);

        eastl::vector<const UIElement*> controls;
        CollectControls(*_levels->GetLayout().root, controls);
        EXPECT_NE(eastl::find(controls.begin(), controls.end(), &Find("LevelEasyCaption")), controls.end()) << "the caption child is checked on its own";
    }

    TEST_F(UIDropdownScreenTest, ComposedOptionsHaveTheRectsOfTheOptionsThatAreButtons) {
        // The same list twice, once as buttons and once as composed elements: every option in the same place, in both views
        for (const auto [width, height] : {std::pair{kViewWidth, kViewHeight}, std::pair{kPortraitWidth, kPortraitHeight}}) {
            ASSERT_NO_FATAL_FAILURE(LoadScene(width, height));
            ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
            eastl::vector<Rect> buttons;
            for (const char* name : kOptionNames) {
                buttons.push_back(Find(name).rect);
            }
            const Rect headerOfButtons = Get<UIDropdown>("Level").rect;

            ASSERT_NO_FATAL_FAILURE(LoadScene(width, height, ComposeOptions));
            ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
            EXPECT_FLOAT_EQ(Get<UIDropdown>("Level").rect.y, headerOfButtons.y);
            for (size_t i = 0; i < kOptionCount; ++i) {
                const Rect& composed = Find(kOptionNames[i]).rect;
                EXPECT_FLOAT_EQ(composed.x, buttons[i].x) << kOptionNames[i];
                EXPECT_FLOAT_EQ(composed.y, buttons[i].y) << kOptionNames[i];
                EXPECT_FLOAT_EQ(composed.w, buttons[i].w) << kOptionNames[i];
                EXPECT_FLOAT_EQ(composed.h, buttons[i].h) << kOptionNames[i];
            }
        }
    }

    TEST_F(UIDropdownScreenTest, AChoiceOfAComposedOptionIsAnActionTheGameAnswersAndTheHeaderFollowsInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, ComposeOptions));
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        EXPECT_EQ(Get<UIDropdown>("Level").GetDisplayText(), eastl::string("Grandmaster"));

        ASSERT_TRUE(Click(Find("LevelEasy")));
        EXPECT_EQ(_game->CountOf("SetLevel"), 1u);
        EXPECT_EQ(_game->LastValue(), "easy"_intern);
        EXPECT_EQ(_game->CountOf("LevelsClose"), 0u) << "the option took the click, not the OK button under it";
        EXPECT_FALSE(Get<UIDropdown>("Level").IsOpen());

        Draw();
        EXPECT_TRUE(Find("LevelEasy").rect.IsEmpty()) << "closed again";
        EXPECT_TRUE(Find("LevelEasyCaption").rect.IsEmpty());
        ForEachLanguage(
            [&](PoolString language) { EXPECT_EQ(Get<UIDropdown>("Level").GetDisplayText(), eastl::string(*CoreManager::GetUIStrings().Find("level.easy"))) << language.ToStringView().data(); });
    }

    TEST_F(UIDropdownScreenTest, APressOutsideAComposedListClosesItAndNothingElseHappens) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, ComposeOptions));
        ASSERT_NO_FATAL_FAILURE(ShowAndOpen());
        const size_t before = _game->Total();
        ASSERT_TRUE(Click(Get<UILabel>("Title")));
        EXPECT_FALSE(Get<UIDropdown>("Level").IsOpen());
        EXPECT_EQ(_game->Total(), before);
    }

}  // namespace BECore::Tests
