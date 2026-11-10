#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UILabel.h>
#include <BECore/UI/UIProgressBar.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UISlider.h>
#include <BECore/UI/UIStack.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UIToggle.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UILayoutMatrix.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The screen test of a settings panel, the template for a screen with the controls of stage 6.4 and with translations (.claude/rules/ui-layout.md, "A screen test").
     * The shape of the panel (the choice panel of UISelectorScreenTest with other rows):
     *
     *  - a small canvas `Hud` with one button, `Open`, whose action shows the panel;
     *  - the panel is a modal `SceneView` canvas on a node of its own (`Settings`), started hidden: a dim, a plate and rows, and an OK button. The rows: Sound (a `UIToggle`,
     *    the caption is the toggle's own text), Music volume (a caption and a `UISlider`, step 0.1), Download (a caption, a `UIProgressBar` and a percent label `{progress}%`),
     *    Language (a caption and a `UISegmented` bound to `language`). Every text is a `{#key}` reference into the string table;
     *  - the game answers a toggle with the variable it is bound to, a slider with the volume, a segment with `UIStrings::SetLanguage` and the variable (`SettingsGame` below).
     *
     * **One run per language.** The validator reads the text that is shown, so a screen is checked once for every language of the table, and a new language added to the table
     * is covered without touching the test (`ForEachLanguage`). The table has `en`, `ru` and `de`, which is deliberately long (its captions together are 40 % or more longer than the English
     * ones, counted in characters, and a test holds that). **The validator is not enough for text:** a label with `overflow="Ellipsis"` (the default) "can shrink", so `TextDoesNotFit` is never
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
        constexpr float kRowHeight = 56.0f;         // a touch target at the smallest fit scale of the matrix (0.571)
        constexpr float kButtonAir = 8.0f;          // the text of a button keeps this much from each edge of its fill
        constexpr float kNarrowestWidth = 480.0f;   // the narrowest window of the matrix, in pixels
        constexpr float kNarrowestHeight = 853.0f;  // 9:16 at that width
        constexpr float kLongerShare = 1.4f;        // the long language is at least this much longer than the English one

        // Two canvases. Hud: one button that opens the panel. Settings: a modal canvas, hidden.
        // Landscape rows are 56 high with the caption on the left. The portrait variant turns the Language row into 32 + 8 + 56 = 96 units (caption above the three segments)
        // and the plate grows with it: 32 padding + 44 title + 4 x 56 + 96 - 56 + 56 OK + 5 x 12 spacing = 456 (416 in landscape).
        constexpr const char* kSettingsXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main" viewWidth="@VIEW_W@" viewHeight="@VIEW_H@"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="SceneView" useSafeArea="true">
        <elements>
          <element type="UIButton" name="Open" anchor="TopRight" x="-12" y="12" width="190" height="56" text="{#settings.open}" fontSize="24" action="SettingsOpen" color="45,75,120,255"/>
        </elements>
      </component>
    </components></node>
    <node name="Settings" layer="20"><components>
      <component type="UICanvasComponent" space="SceneView" useSafeArea="true" modal="true" visible="false">
        <elements>
          <element type="UIElement" name="Dim" anchor="Stretch" ignoreSafeArea="true" color="0,0,0,150" blocksInput="true"/>
          <element type="UIStack" name="Plate" anchor="Center" width="94%" maxWidth="640" height="416" direction="Vertical" spacing="12" padding="16" crossAlign="Stretch" color="36,40,52,255" blocksInput="true">
            <children>
              <element type="UILabel" name="Title" height="44" text="{#settings.title}" fontSize="30" align="Center"/>
              <element type="UIToggle" name="Sound" bind="soundOn" action="SettingsSound" height="56" text="{#settings.sound}" fontSize="24" verticalAlign="Center"/>
              <element type="UIStack" name="MusicRow" height="56" direction="Horizontal" spacing="8" crossAlign="Center">
                <children>
                  <element type="UILabel" name="MusicCaption" width="220" height="56" text="{#settings.music}" fontSize="24" verticalAlign="Center"/>
                  <element type="UISlider" name="Volume" bind="volume" action="SettingsVolume" minValue="0" maxValue="1" step="0.1" flex="1" height="56"/>
                </children>
              </element>
              <element type="UIStack" name="DownloadRow" height="56" direction="Horizontal" spacing="8" crossAlign="Center">
                <children>
                  <element type="UILabel" name="DownloadCaption" width="220" height="56" text="{#settings.download}" fontSize="24" verticalAlign="Center"/>
                  <element type="UIProgressBar" name="Progress" bind="progress" minValue="0" maxValue="100" flex="1" height="24"/>
                  <element type="UILabel" name="Percent" width="64" height="56" text="{progress}%" fontSize="24" align="Right" verticalAlign="Center"/>
                </children>
              </element>
              <element type="UIStack" name="LanguageRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
                <children>
                  <element type="UILabel" name="LanguageCaption" width="220" height="56" text="{#settings.language}" fontSize="24" verticalAlign="Center"/>
                  <element type="UISegmented" name="Language" bind="language" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255">
                    <children>
                      <element type="UIButton" name="LanguageEn" flex="1" height="56" text="{#lang.en}" fontSize="24" value="en" action="SettingsLanguage" color="70,70,100,255"/>
                      <element type="UIButton" name="LanguageRu" flex="1" height="56" text="{#lang.ru}" fontSize="24" value="ru" action="SettingsLanguage" color="70,70,100,255"/>
                      <element type="UIButton" name="LanguageDe" flex="1" height="56" text="{#lang.de}" fontSize="24" value="de" action="SettingsLanguage" color="70,70,100,255"/>
                    </children>
                  </element>
                </children>
              </element>
              <element type="UIButton" name="Ok" height="56" text="{#settings.ok}" fontSize="26" action="SettingsClose" color="40,90,60,255"/>
            </children>
          </element>
        </elements>
        <variants>
          <variant when="orientation:portrait">
            <overrides>
              <set element="Plate" height="456"/>
              <set element="LanguageRow" direction="Vertical" height="96"/>
              <set element="LanguageCaption" height="32"/>
            </overrides>
          </variant>
        </variants>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        // English, Russian and a deliberately long test language. The names of the languages are the same in all three (each language is written in itself)
        constexpr const char* kStringsXml = R"(
<UIStrings default="en">
  <language id="en">
    <string key="settings.open" text="Settings"/>
    <string key="settings.title" text="Settings"/>
    <string key="settings.sound" text="Sound"/>
    <string key="settings.music" text="Music volume"/>
    <string key="settings.download" text="Download"/>
    <string key="settings.language" text="Language"/>
    <string key="settings.ok" text="OK"/>
    <string key="lang.en" text="English"/>
    <string key="lang.ru" text="Русский"/>
    <string key="lang.de" text="Deutsch"/>
  </language>
  <language id="ru">
    <string key="settings.open" text="Настройки"/>
    <string key="settings.title" text="Настройки"/>
    <string key="settings.sound" text="Звук"/>
    <string key="settings.music" text="Громкость музыки"/>
    <string key="settings.download" text="Загрузка"/>
    <string key="settings.language" text="Язык"/>
    <string key="settings.ok" text="Готово"/>
    <string key="lang.en" text="English"/>
    <string key="lang.ru" text="Русский"/>
    <string key="lang.de" text="Deutsch"/>
  </language>
  <language id="de">
    <string key="settings.open" text="Einstellungen"/>
    <string key="settings.title" text="Einstellungen ändern"/>
    <string key="settings.sound" text="Geräuscheffekte"/>
    <string key="settings.music" text="Lautstärke der Musik"/>
    <string key="settings.download" text="Herunterladen"/>
    <string key="settings.language" text="Sprachauswahl"/>
    <string key="settings.ok" text="Übernehmen"/>
    <string key="lang.en" text="English"/>
    <string key="lang.ru" text="Русский"/>
    <string key="lang.de" text="Deutsch"/>
  </language>
</UIStrings>)";

        constexpr const char* kLanguageKeys[] = {"settings.title", "settings.sound", "settings.music", "settings.download", "settings.language", "settings.ok"};

        struct Language {
            const char* id;
            const char* title;    // what the title shows in it
            const char* segment;  // the name of its segment in the Language group
        };

        const Language kLanguages[] = {{"en", "Settings", "LanguageEn"}, {"ru", "Настройки", "LanguageRu"}, {"de", "Einstellungen ändern", "LanguageDe"}};

        // Every control of the panel that shows a text of the table
        constexpr const char* kTextControlNames[] = {"Title", "Sound", "MusicCaption", "DownloadCaption", "Percent", "LanguageCaption", "LanguageEn", "LanguageRu", "LanguageDe", "Ok"};

        std::string SceneXml(float viewWidth, float viewHeight, const std::function<void(std::string&)>& edit = {}) {
            std::string xml = kSettingsXml;
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

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        SDL_MouseMotionEvent Motion(float x, float y, SDL_MouseButtonFlags held = 0) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.x = x;
            event.y = y;
            event.state = held;
            return event;
        }

        /// The number of characters (code points) of a UTF-8 text: the length of a word, whatever the language
        size_t Characters(eastl::string_view text) {
            size_t count = 0;
            for (const char c : text) {
                if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) {
                    ++count;
                }
            }
            return count;
        }

        /// What reaches game code instead of a canvas: the board under the panel
        class BoardClicks : public SubscriptionHolder {
        public:
            BoardClicks() {
                Subscribe<SDLEvents::MouseButtonDownEvent, &BoardClicks::OnDown>(this);
            }

            int downs = 0;

        private:
            void OnDown(const SDLEvents::MouseButtonDownEvent&) {
                ++downs;
            }
        };

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
            eastl::vector<eastl::string> ValuesOf(const char* action) const {
                eastl::vector<eastl::string> values;
                for (size_t i = 0; i < _actions.size(); ++i) {
                    if (_actions[i] == PoolString::Intern(action)) {
                        values.push_back(eastl::string(_values[i].ToStringView()));
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
            eastl::vector<PoolString> _actions;
            eastl::vector<PoolString> _values;
        };

        /// Every control of the tree that shows text or takes a press, depth first (the root itself is the canvas and has none)
        void CollectControls(const UIElement& element, eastl::vector<const UIElement*>& found) {
            if (element.Cast<UILabel>() || element.Cast<UIButton>() || element.Cast<UIToggle>() || element.Cast<UISlider>()) {
                found.push_back(&element);
            }
            for (const auto& child : element.children) {
                CollectControls(*child, found);
            }
        }

        eastl::string ShownText(const UIElement& element) {
            const UILabel* label = element.Cast<UILabel>();
            if (!label) {
                label = element.Cast<UIButton>();
            }
            if (!label) {
                label = element.Cast<UIToggle>();
            }
            return label ? eastl::string(label->GetDisplayText()) : eastl::string();
        }

        /// What is wrong with a laid-out panel in the language that is set now, one problem to a line, nothing when it is right: a control that is not shown at all (an empty
        /// rect), text that its control cuts (the validator does not see it for an ellipsis label; a toggle is asked for the area left of its switch), and a control that takes a
        /// press and is under kRowHeight
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
                if (control->interactive && control->rect.h < kRowHeight - 0.01f) {
                    problems += Format("{} is {:.1f} units high, under {:.0f}\n", control->name.ToStringView(), control->rect.h, kRowHeight);
                }
            }
            return problems;
        }

        bool HasIssue(const eastl::vector<UIMatrixFailure>& failures, UILayoutIssueKind kind) {
            for (const auto& failure : failures) {
                for (const UILayoutIssue& issue : failure.issues) {
                    if (issue.kind == kind) {
                        return true;
                    }
                }
            }
            return false;
        }

        /// Runs @p check once for every language of the table and puts the language that was current back. A language added to the table is covered without touching a test
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

    class UISettingsScreenTest : public ::testing::Test {
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
            SetVars("true", "0.5", "40", "en");
        }

        void TearDown() override {
            _game.reset();
            _hud.Reset();
            _settings.Reset();
            _manager.Reset();
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();  // the table is global: the next test starts with none, and with no current language
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        /// The table of the screen, through the reader of the file, so the test covers the file format as well; the first language is the default one and the current one
        static void LoadStrings(const char* xml) {
            UIStrings& strings = CoreManager::GetUIStrings();
            strings.Clear();
            auto config = XmlConfig::Create();
            ASSERT_TRUE(config.LoadFromString(xml));
            const eastl::vector<eastl::string> problems = strings.LoadFromXml(config.GetRoot());
            EXPECT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0].c_str());
            ASSERT_EQ(strings.GetLanguages().size(), 3u);
        }

        static void SetVars(const char* soundOn, const char* volume, const char* progress, const char* language) {
            CoreManager::GetUIVars().Set("soundOn"_intern, soundOn);
            CoreManager::GetUIVars().Set("volume"_intern, volume);
            CoreManager::GetUIVars().Set("progress"_intern, progress);
            CoreManager::GetUIVars().Set("language"_intern, language);
        }

        /// The scene of the panel with a view of this size; the output has the same size, so a point of the view is a pixel
        void LoadScene(float viewWidth = kViewWidth, float viewHeight = kViewHeight, const std::function<void(std::string&)>& edit = {}) {
            _game.reset();
            _hud.Reset();
            _settings.Reset();
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
                } else if (node->GetName() == "Settings"_intern) {
                    _settings = node->GetComponent<UICanvasComponent>();
                }
            }
            ASSERT_TRUE(_hud);
            ASSERT_TRUE(_settings);
            _hud->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
            _settings->UpdateLayout(1280.0f, 720.0f);
            _game.emplace(GetScene());
        }

        Scene& GetScene() const {
            return *_manager->GetSceneByName("Main"_intern);
        }

        void Show() {
            ASSERT_TRUE(UICanvasComponent::SetVisibleByName(GetScene(), "Settings"_intern, true));
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
            UIElement* element = _settings->GetRoot().FindByName(PoolString::Intern(name));
            T* typed = element ? element->Cast<T>() : nullptr;
            if (typed == nullptr) {
                ADD_FAILURE() << name << (element ? " is not the expected control" : " is not in the panel");
                std::abort();  // a reference to nothing cannot be returned; the failure above is printed first
            }
            return *typed;
        }

        static std::pair<float, float> CenterOf(const UIElement& element) {
            return {element.rect.x + element.rect.w * 0.5f, element.rect.y + element.rect.h * 0.5f};
        }

        std::pair<bool, bool> ClickAt(float x, float y) {
            const bool pressed = CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));
            const bool released = CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
            return {pressed, released};
        }

        std::pair<bool, bool> Click(const UIElement& element) {
            const auto [x, y] = CenterOf(element);
            return ClickAt(x, y);
        }

        /// The x of the slider's thumb centre when the value stands at @p fraction of the travel
        float XAt(float fraction) const {
            const UISlider& slider = Get<UISlider>("Volume");
            const UISlider::Parts parts = UISlider::GetParts(slider.rect, fraction);
            return parts.travelStart + (parts.travelEnd - parts.travelStart) * fraction;
        }

        float VolumeY() const {
            return CenterOf(Get<UISlider>("Volume")).second;
        }

        /// The panel on the whole matrix with the options of the game, once for every language: the findings of the validator, each case named with its language
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

        /// The panel in the narrowest window of the matrix, alone: 480 px wide with the side insets of a phone held sideways
        void LayOutInTheNarrowestWindow(UICanvasLayout& layout) const {
            UILayoutContext context = ViewContext();
            context.outputWidth = kNarrowestWidth;
            context.outputHeight = kNarrowestHeight;
            context.safeArea = UIPreview::SimulatedSafeArea(SafeAreaSimulation::SideInsets, kNarrowestWidth, kNarrowestHeight);
            layout.Update(context);
        }

        /// Problems() in the narrowest window, in every language
        eastl::string ProblemsInTheNarrowestWindow(UICanvasComponent& canvas) const {
            eastl::string report;
            UICanvasLayout& layout = canvas.GetLayout();
            ForEachLanguage([&](PoolString language) {
                LayOutInTheNarrowestWindow(layout);
                const eastl::string problems = Problems(layout);
                if (!problems.empty()) {
                    report += Format("[{}] {}", language.ToStringView(), problems.c_str());
                }
            });
            layout.Invalidate();
            return report;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _hud;
        IntrusivePtr<UICanvasComponent> _settings;
        std::optional<SettingsGame> _game;
    };

    // ---- the size matrix, once per language ------------------------------------------------------------------------------

    TEST_F(UISettingsScreenTest, ThePanelPassesTheSizeMatrixInALandscapeViewInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        const auto failures = ValidatePanel(*_settings);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        const auto hudFailures = ValidatePanel(*_hud);
        EXPECT_TRUE(hudFailures.empty()) << DescribeMatrixFailures(hudFailures).c_str();
    }

    TEST_F(UISettingsScreenTest, ThePanelPassesTheSizeMatrixInAPortraitViewInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kPortraitWidth, kPortraitHeight));
        const auto failures = ValidatePanel(*_settings);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(UISettingsScreenTest, NoWordIsCutOnAnyCaseOfTheMatrixInAnyLanguage) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        const eastl::string landscape = ProblemsOnMatrix(*_settings);
        EXPECT_TRUE(landscape.empty()) << landscape.c_str();
        const eastl::string hud = ProblemsOnMatrix(*_hud);
        EXPECT_TRUE(hud.empty()) << hud.c_str();

        ASSERT_NO_FATAL_FAILURE(LoadScene(kPortraitWidth, kPortraitHeight));
        const eastl::string portrait = ProblemsOnMatrix(*_settings);
        EXPECT_TRUE(portrait.empty()) << portrait.c_str();
    }

    TEST_F(UISettingsScreenTest, NoWordIsCutInTheNarrowestWindowAndEveryRowIsATouchTarget) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        UICanvasLayout& layout = _settings->GetLayout();
        LayOutInTheNarrowestWindow(layout);

        const float scale = kNarrowestWidth / kViewWidth;
        EXPECT_NEAR(layout.GetResolved().scale, scale, 0.01f);
        EXPECT_NEAR(layout.root->rect.w, kNarrowestWidth * (1.0f - 2.0f * UIPreview::kSideInsetShare) / scale, 0.5f) << "the safe area, in view units: about 689";

        const eastl::string problems = ProblemsInTheNarrowestWindow(*_settings);
        EXPECT_TRUE(problems.empty()) << problems.c_str();

        LayOutInTheNarrowestWindow(layout);
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        for (const char* name : {"Sound", "Volume", "LanguageEn", "LanguageRu", "LanguageDe", "Ok"}) {
            const UIElement* element = _settings->GetRoot().FindByName(PoolString::Intern(name));
            ASSERT_NE(element, nullptr) << name;
            EXPECT_GE(element->rect.h, kRowHeight - 0.01f) << name;
            EXPECT_GE(std::min(element->rect.w, element->rect.h) * layout.GetResolved().scale, minTouch - 0.01f) << name << " is a touch target on the smallest window";
        }
        layout.Invalidate();
    }

    TEST_F(UISettingsScreenTest, EveryWordIsAskedInEveryLanguageNotOnlyTheLongestString) {
        // The same walk as Problems(), written out: each control with text, with the width it got on the narrowest window, in each language
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        UICanvasLayout& layout = _settings->GetLayout();
        size_t languages = 0;
        ForEachLanguage([&](PoolString language) {
            ++languages;
            LayOutInTheNarrowestWindow(layout);
            const float scale = layout.GetResolved().scale;
            for (const char* name : kTextControlNames) {
                const UIElement* element = _settings->GetRoot().FindByName(PoolString::Intern(name));
                ASSERT_NE(element, nullptr) << name;
                const float air = element->Cast<UIButton>() ? kButtonAir : 0.0f;
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
            english += Characters(*strings.Find(key));
            ASSERT_TRUE(strings.SetLanguage("de"_intern));
            german += Characters(*strings.Find(key));
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
            const UIElement* element = _settings->GetRoot().FindByName(PoolString::Intern(row));
            ASSERT_NE(element, nullptr) << row;
            EXPECT_GE(element->rect.h, kRowHeight - 0.01f) << row;
        }
        const struct {
            const char* caption;
            const char* control;
        } pairs[] = {{"MusicCaption", "Volume"}, {"DownloadCaption", "Progress"}, {"LanguageCaption", "Language"}};
        for (const auto& pair : pairs) {
            const UIElement* caption = _settings->GetRoot().FindByName(PoolString::Intern(pair.caption));
            const UIElement* control = _settings->GetRoot().FindByName(PoolString::Intern(pair.control));
            ASSERT_TRUE(caption && control) << pair.caption;
            EXPECT_LT(caption->rect.x + caption->rect.w, control->rect.x + 0.01f) << pair.caption;
            EXPECT_GE(control->rect.w, 200.0f) << pair.control << " keeps room to be used";
        }
        const UISlider& volume = Get<UISlider>("Volume");
        EXPECT_NEAR(volume.rect.h, kRowHeight, 0.01f) << "the thumb is the height of the slider: a touch target";
        const UIElement& bar = Get<UIProgressBar>("Progress");
        const UIElement& row = Get<UIStack>("DownloadRow");
        EXPECT_NEAR(bar.rect.y + bar.rect.h * 0.5f, row.rect.y + row.rect.h * 0.5f, 0.01f) << "the thin bar is centered in its row";
        EXPECT_LT(bar.rect.x + bar.rect.w, Get<UILabel>("Percent").rect.x + 0.01f);
    }

    TEST_F(UISettingsScreenTest, InAPortraitViewTheLanguageCaptionIsAboveItsGroupAndTheSegmentsKeepTheirHeight) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kPortraitWidth, kPortraitHeight));
        Draw();
        const UIElement& caption = Get<UILabel>("LanguageCaption");
        const UIElement& group = Get<UISegmented>("Language");
        EXPECT_LT(caption.rect.y + caption.rect.h, group.rect.y + 0.01f);
        EXPECT_NEAR(caption.rect.x, group.rect.x, 0.01f);
        EXPECT_NEAR(group.rect.h, kRowHeight, 0.01f);
        const UIElement& plate = Get<UIStack>("Plate");
        EXPECT_NEAR(plate.rect.h, 456.0f, 0.01f);
        EXPECT_GE(plate.rect.y, 0.0f);
        EXPECT_LE(plate.rect.y + plate.rect.h, kPortraitHeight);
        EXPECT_GE(Get<UISlider>("Volume").rect.h, kRowHeight - 0.01f);
    }

    // ---- hidden, shown (modal) ---------------------------------------------------------------------------------------------

    TEST_F(UISettingsScreenTest, ThePanelHasAHiddenModalCanvasAtFirstAndDrawsNothingUntilShown) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        EXPECT_FALSE(_settings->_visible);
        EXPECT_TRUE(_settings->_modal);
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
        const std::pair<float, float> points[] = {CenterOf(Get<UIToggle>("Sound")), {plate.rect.x + 4.0f, plate.rect.y + 4.0f}, {5.0f, 5.0f}, {kViewWidth - 5.0f, kViewHeight - 5.0f}};
        for (const auto& [x, y] : points) {
            const auto [pressed, released] = ClickAt(x, y);
            EXPECT_TRUE(pressed) << x << "," << y;
            EXPECT_TRUE(released) << x << "," << y;
        }
        EXPECT_EQ(board.downs, 0) << "a modal canvas keeps the press from the board";
        EXPECT_EQ(_game->Total(), 1u) << "only the click on the toggle is an action";

        const auto [x, y] = CenterOf(Get<UISlider>("Volume"));
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
        EXPECT_TRUE(_settings->_visible);
        Draw();
        Click(Get<UIButton>("Ok"));
        EXPECT_EQ(_game->LastAction(), "SettingsClose"_intern);
        EXPECT_FALSE(_settings->_visible);
    }

    // ---- the controls: toggle, slider, progress bar, language -------------------------------------------------------------

    TEST_F(UISettingsScreenTest, TheToggleAsksForTheOppositeValueAndTheGameMovesTheSwitch) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        const UIToggle& sound = Get<UIToggle>("Sound");
        ASSERT_TRUE(sound.IsOn());

        const auto [pressed, released] = Click(sound);
        EXPECT_TRUE(pressed && released);
        EXPECT_EQ(_game->LastAction(), "SettingsSound"_intern);
        EXPECT_EQ(_game->LastValue(), "false"_intern) << "the toggle is on, so it asks for off";
        Draw();
        EXPECT_FALSE(sound.IsOn()) << "the game answered with the variable";

        Click(sound);
        EXPECT_EQ(_game->LastValue(), "true"_intern);
        Draw();
        EXPECT_TRUE(sound.IsOn());
    }

    TEST_F(UISettingsScreenTest, AToggleClickChangesNothingByItselfWhenTheGameDoesNotAnswer) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        _game.reset();
        const UIToggle& sound = Get<UIToggle>("Sound");
        Click(sound);
        Draw();
        EXPECT_TRUE(sound.IsOn());
        EXPECT_EQ(CoreManager::GetUIVars().Get("soundOn"_intern), "true");
    }

    TEST_F(UISettingsScreenTest, TheThumbOfTheToggleStandsAtTheRightWhenOnAndAtTheLeftWhenOff) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        const UIToggle& sound = Get<UIToggle>("Sound");
        const UIToggle::Parts parts = UIToggle::GetParts(sound.rect);
        EXPECT_GT(parts.thumbOn.x, parts.thumbOff.x);
        EXPECT_GE(parts.track.x, parts.text.x + parts.text.w - 0.01f) << "the caption never runs under the switch";
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
        const UISlider& volume = Get<UISlider>("Volume");
        EXPECT_NEAR(volume.GetFraction(), 0.5f, 1e-4f);
        const float y = VolumeY();

        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(XAt(0.3f), y, true)));
        for (const float fraction : {0.4f, 0.5f, 0.6f, 0.7f}) {
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(XAt(fraction), y, SDL_BUTTON_LMASK));
            CoreManager::GetUIInput().DispatchMouseMotion(Motion(XAt(fraction) + 1.0f, y, SDL_BUTTON_LMASK));  // inside one step: silent
        }
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(XAt(0.7f), y, false)));

        const eastl::vector<eastl::string> values = _game->ValuesOf("SettingsVolume");
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

        const eastl::vector<eastl::string> values = _game->ValuesOf("SettingsVolume");
        ASSERT_EQ(values.size(), 3u);
        EXPECT_EQ(values[0], "0.5");
        EXPECT_EQ(values[1], "1") << "past the end: the maximum, written without a decimal point";
        EXPECT_EQ(values[2], "0");
        Draw();
        const UISlider& volume = Get<UISlider>("Volume");
        const UISlider::Parts parts = UISlider::GetParts(volume.rect, volume.GetFraction());
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
        EXPECT_NEAR(Get<UISlider>("Volume").GetFraction(), 0.5f, 1e-4f);
    }

    TEST_F(UISettingsScreenTest, TheProgressBarFollowsItsVariableAndTakesNoClick) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        const UIProgressBar& bar = Get<UIProgressBar>("Progress");
        EXPECT_NEAR(bar.GetFraction(), 0.4f, 1e-4f);
        EXPECT_EQ(ShownText(Get<UILabel>("Percent")), "40%");

        for (const char* value : {"0", "75", "100", "250", "abc"}) {
            CoreManager::GetUIVars().Set("progress"_intern, value);
            Draw();
            const float expected = std::string(value) == "75" ? 0.75f : (std::string(value) == "100" || std::string(value) == "250") ? 1.0f : 0.0f;
            EXPECT_NEAR(bar.GetFraction(), expected, 1e-4f) << value;
            EXPECT_EQ(ShownText(Get<UILabel>("Percent")), Format("{}%", value).c_str()) << value;
        }
        const size_t before = _game->Total();
        Click(bar);
        EXPECT_EQ(_game->Total(), before) << "a bar is not a button";
    }

    TEST_F(UISettingsScreenTest, TheLanguageSegmentsEmitTheirValueAndTheGameSwitchesEveryCaptionAtTheNextDraw) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        EXPECT_TRUE(Get<UIButton>("LanguageEn").selected);
        EXPECT_EQ(ShownText(Get<UILabel>("Title")), "Settings");

        for (const Language& language : kLanguages) {
            Draw();  // the layout of the narrowest window below is not the one the clicks are aimed at
            Click(Get<UIButton>(language.segment));
            EXPECT_EQ(_game->LastAction(), "SettingsLanguage"_intern) << language.id;
            EXPECT_EQ(_game->LastValue(), PoolString::Intern(language.id));
            Draw();
            EXPECT_EQ(CoreManager::GetUIStrings().GetLanguage(), PoolString::Intern(language.id));
            EXPECT_EQ(ShownText(Get<UILabel>("Title")), language.title) << "the title follows the language at the next draw";
            EXPECT_EQ(ShownText(*_hud->GetRoot().FindByName("Open"_intern)), *CoreManager::GetUIStrings().Find("settings.open")) << "the button of the other canvas follows too";
            EXPECT_TRUE(Get<UIButton>(language.segment).selected);
            EXPECT_EQ(Get<UISegmented>("Language").GetSelectedValue(), language.id);
            // The names of the languages are the same in every language
            EXPECT_EQ(ShownText(Get<UIButton>("LanguageDe")), "Deutsch");
            // The layout is still valid in the narrowest window, in the language that is set now
            UICanvasLayout& layout = _settings->GetLayout();
            LayOutInTheNarrowestWindow(layout);
            EXPECT_TRUE(Problems(layout).empty()) << language.id << ": " << Problems(layout).c_str();
            layout.Invalidate();
        }
    }

    TEST_F(UISettingsScreenTest, AllWordsOfTheScreenChangeWithTheLanguageAndOnlyTheNamesOfTheLanguagesStay) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_NO_FATAL_FAILURE(Show());
        const char* const names[] = {"Title", "Sound", "MusicCaption", "DownloadCaption", "LanguageCaption", "Ok"};
        eastl::vector<eastl::string> english;
        for (const char* name : names) {
            english.push_back(ShownText(*_settings->GetRoot().FindByName(PoolString::Intern(name))));
        }
        for (const char* id : {"ru", "de"}) {
            ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage(PoolString::Intern(id)));
            Draw();
            for (size_t i = 0; i < english.size(); ++i) {
                const eastl::string shown = ShownText(*_settings->GetRoot().FindByName(PoolString::Intern(names[i])));
                EXPECT_NE(shown, english[i]) << id << " " << names[i];
                EXPECT_FALSE(shown.empty()) << id << " " << names[i];
            }
        }
    }

    // ---- a missing translation ---------------------------------------------------------------------------------------------

    TEST_F(UISettingsScreenTest, AKeyMissingInTheLongLanguageFallsBackToEnglishAndStillFits) {
        std::string table = kStringsXml;
        const size_t at = table.find(R"(<string key="settings.ok" text="Übernehmen"/>)");
        ASSERT_NE(at, std::string::npos);
        table.erase(at, std::string(R"(<string key="settings.ok" text="Übernehmen"/>)").size());
        ASSERT_NO_FATAL_FAILURE(LoadStrings(table.c_str()));
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("de"_intern));
        Draw();
        EXPECT_EQ(ShownText(Get<UIButton>("Ok")), "OK") << "the default language answers";
        EXPECT_TRUE(CoreManager::GetUIStrings().GetMissing().empty()) << "a fallback is not a missing key";
        UICanvasLayout& layout = _settings->GetLayout();
        LayOutInTheNarrowestWindow(layout);
        EXPECT_TRUE(Problems(layout).empty()) << Problems(layout).c_str();
        layout.Invalidate();
    }

    // ---- a deliberately broken copy must be caught ------------------------------------------------------------------------

    TEST_F(UISettingsScreenTest, ASliderUnderTheTouchSizeIsCaughtByTheValidator) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](std::string& xml) { Replace(xml, R"(step="0.1" flex="1" height="56")", R"(step="0.1" flex="1" height="30")"); }));
        EXPECT_NEAR(Get<UISlider>("Volume").rect.h, 30.0f, 0.01f);
        const auto failures = ValidatePanel(*_settings);
        EXPECT_TRUE(HasIssue(failures, UILayoutIssueKind::TouchTargetTooSmall)) << DescribeMatrixFailures(failures).c_str();
        EXPECT_NE(ProblemsOnMatrix(*_settings).find("Volume is 30.0 units high"), eastl::string::npos);
    }

    TEST_F(UISettingsScreenTest, ASliderWithoutAnExplicitHeightHasNoRectAndIsCaughtHere) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](std::string& xml) { Replace(xml, R"(step="0.1" flex="1" height="56")", R"(step="0.1" flex="1")"); }));
        ASSERT_NO_FATAL_FAILURE(Show());
        EXPECT_NEAR(Get<UISlider>("Volume").rect.h, 0.0f, 0.01f);
        UICanvasLayout& layout = _settings->GetLayout();
        const eastl::string problems = Problems(layout);
        EXPECT_NE(problems.find("Volume '' has an empty rect"), eastl::string::npos) << problems.c_str();
    }

    TEST_F(UISettingsScreenTest, AToggleCaptionTooLongForItsAreaIsCaughtByTheFitCheckAndNotByTheValidator) {
        const auto lengthen = [](std::string& xml) { Replace(xml, R"(text="{#settings.sound}")", R"(text="Sound effects, music and every other noise of the game")"); };
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, lengthen));
        EXPECT_FALSE(HasIssue(ValidatePanel(*_settings), UILayoutIssueKind::TextDoesNotFit)) << "an ellipsis label can shrink: the validator does not report it";
        const eastl::string matrix = ProblemsOnMatrix(*_settings);
        EXPECT_NE(matrix.find("Sound cuts 'Sound effects"), eastl::string::npos) << matrix.c_str();

        UICanvasLayout& layout = _settings->GetLayout();
        LayOutInTheNarrowestWindow(layout);
        EXPECT_NE(Problems(layout).find("Sound cuts 'Sound effects"), eastl::string::npos);
        layout.Invalidate();
    }

    TEST_F(UISettingsScreenTest, AWordTooLongInTheLongLanguageIsCaughtInTheNarrowestWindowAlone) {
        // A German caption made too long for the area left of the switch; the narrowest window (about 689 units of safe width) finds it, and the failure names the language
        std::string table = kStringsXml;
        const std::string original = R"(text="Geräuscheffekte")";
        const size_t at = table.find(original);
        ASSERT_NE(at, std::string::npos);
        table.replace(at, original.size(), R"(text="Geräuscheffekte und Klangeinstellungen des Spiels")");
        ASSERT_NO_FATAL_FAILURE(LoadStrings(table.c_str()));
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        const eastl::string problems = ProblemsInTheNarrowestWindow(*_settings);
        EXPECT_NE(problems.find("[de] Sound cuts 'Geräuscheffekte und"), eastl::string::npos) << problems.c_str();
        EXPECT_EQ(problems.find("[en]"), eastl::string::npos) << "English and Russian are fine: the failure names its language";
        EXPECT_EQ(problems.find("[ru]"), eastl::string::npos);
    }

    TEST_F(UISettingsScreenTest, AKeyMissingEverywhereShowsTheKeyInTheLongLanguageAndTheFitCheckStillMeasuresIt) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight,
                                          [](std::string& xml) { Replace(xml, R"(text="{#settings.sound}")", R"(text="{#settings.sound.effects.volume.and.noise.of.the.game.and.of.the.menu}")"); }));
        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("de"_intern));
        Draw();
        EXPECT_EQ(ShownText(Get<UIToggle>("Sound")), "settings.sound.effects.volume.and.noise.of.the.game.and.of.the.menu") << "the key itself, without the braces: never an empty label";
        ASSERT_EQ(CoreManager::GetUIStrings().GetMissing().size(), 1u);
        EXPECT_EQ(CoreManager::GetUIStrings().GetMissing()[0].language, "de"_intern);

        UICanvasLayout& layout = _settings->GetLayout();
        LayOutInTheNarrowestWindow(layout);
        const eastl::string problems = Problems(layout);
        EXPECT_NE(problems.find("Sound cuts 'settings.sound.effects.volume.and.noise.of.the.game.and.of.the.menu'"), eastl::string::npos) << problems.c_str();
        layout.Invalidate();
    }

}  // namespace BECore::Tests
