#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UISegmented.h>
#include <BECore/UI/UIStack.h>
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
     * The screen test of a choice panel, the template for a game that has one (the Chess opponent panel is the first: mode, color, engine and level as four groups).
     * It is the shape that .claude/rules/ui-controls.md ("A choice panel") points to:
     *
     *  - a small canvas `Hud` with one button, `NewGame`, whose action opens the panel;
     *  - the panel is a modal `SceneView` canvas on a node of its own (`Selector`), started hidden: a dim, a plate and four rows, each a caption beside a `UISegmented`
     *    (above it in a portrait view), and an OK button. The game shows and hides it with `UICanvasComponent::SetVisibleByName`;
     *  - every segment carries `action` + `value`; the game answers a click by setting the variable the group is bound to (`SelectorGame` below plays that part).
     *
     * **A segment needs an explicit `height` in XML**, also with `flex="1"`: flex shares the main axis (the width) and a segment without a height gets a rect of
     * height 0, so it is not drawn, cannot be hit (`ui_click` answers "hidden") and the validator, which skips an empty rect, does not see it. The same holds for a caption
     * in a row that turns vertical in a variant.
     *
     * **The validator is not enough for text.** A label with `overflow="Ellipsis"` (the default) "can shrink", so `TextDoesNotFit` is never reported for it; `Problems()` asks
     * `CheckContent()` of every control on the text that is shown (a button must also keep kButtonAir between its text and the edge of its fill), once per case of the
     * matrix and for the narrowest window alone. The font is proportional, so every word is asked, not only the longest string.
     */

    namespace {

        constexpr float kViewWidth = 840.0f;  // the Chess view: the board 640 wide and 100 units on each side
        constexpr float kViewHeight = 800.0f;
        constexpr float kPortraitWidth = 560.0f;  // a portrait view: its longer side stays at most 840 units, so 56 units are still 32 px in a 480 px square window
        constexpr float kPortraitHeight = 840.0f;
        constexpr float kSegmentHeight = 56.0f;     // a touch target at the smallest fit scale of the matrix (0.571)
        constexpr float kButtonAir = 8.0f;          // the text of a button keeps this much from each edge of its fill
        constexpr float kNarrowestWidth = 480.0f;   // the narrowest window of the matrix, in pixels
        constexpr float kNarrowestHeight = 853.0f;  // 9:16 at that width

        // Two canvases. Hud: one button that opens the panel.
        // Selector: a modal canvas, hidden. Landscape rows are 56 high with the caption on the left; the portrait variant (a view taller than wide) turns every row
        // into 32 + 8 + 56 = 96 units, caption above group, and the plate grows with it: 32 padding + 44 title + 4 x 96 + 56 OK + 5 x 12 spacing = 576 (416 in landscape).
        // The same variant sets the font of the Level segments to 22: a quarter of a narrow plate on a window with side insets is too little for "Medium" at 24 with its air.
        constexpr const char* kSelectorXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main" viewWidth="@VIEW_W@" viewHeight="@VIEW_H@"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="SceneView" useSafeArea="true">
        <elements>
          <element type="UIElement" name="NewGame" anchor="TopRight" x="-12" y="12" width="190" height="56" fontSize="24" color="45,75,120,255">
            <components>
              <component type="UITextComponent" text="Opponent" align="Center" verticalAlign="Center" wrap="false"/>
              <component type="UIInputClicker" action="SelectorOpen"/>
            </components>
          </element>
        </elements>
      </component>
    </components></node>
    <node name="Selector" layer="20"><components>
      <component type="UICanvasComponent" space="SceneView" useSafeArea="true" modal="true" visible="false">
        <elements>
          <element type="UIElement" name="Dim" anchor="Stretch" ignoreSafeArea="true" color="0,0,0,150" blocksInput="true"/>
          <element type="UIStack" name="Plate" anchor="Center" width="94%" maxWidth="640" height="416" direction="Vertical" spacing="12" padding="16" crossAlign="Stretch" color="36,40,52,255" blocksInput="true">
            <children>
              <element type="UIElement" name="Title" height="44" fontSize="30">
                <components><component type="UITextComponent" text="Opponent" align="Center"/></components>
              </element>
              <element type="UIStack" name="ModeRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
                <children>
                  <element type="UIElement" name="ModeCaption" width="150" height="56" fontSize="24">
                    <components><component type="UITextComponent" text="Opponent" verticalAlign="Center"/></components>
                  </element>
                  <element type="UISegmented" name="Mode" bind="opponentMode" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255">
                    <children>
                      <element type="UIElement" name="ModeTwo" flex="1" height="56" fontSize="24" color="70,70,100,255">
                        <components>
                          <component type="UITextComponent" text="Two players" align="Center" verticalAlign="Center" wrap="false"/>
                          <component type="UIInputClicker" action="SelectorMode" value="two"/>
                        </components>
                      </element>
                      <element type="UIElement" name="ModeComputer" flex="1" height="56" fontSize="24" color="70,70,100,255">
                        <components>
                          <component type="UITextComponent" text="Computer" align="Center" verticalAlign="Center" wrap="false"/>
                          <component type="UIInputClicker" action="SelectorMode" value="computer"/>
                        </components>
                      </element>
                    </children>
                  </element>
                </children>
              </element>
              <element type="UIStack" name="ColorRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
                <children>
                  <element type="UIElement" name="ColorCaption" width="150" height="56" fontSize="24">
                    <components><component type="UITextComponent" text="Play as" verticalAlign="Center"/></components>
                  </element>
                  <element type="UISegmented" name="Color" bind="opponentColor" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255">
                    <children>
                      <element type="UIElement" name="ColorWhite" flex="1" height="56" fontSize="24" color="70,70,100,255">
                        <components>
                          <component type="UITextComponent" text="White" align="Center" verticalAlign="Center" wrap="false"/>
                          <component type="UIInputClicker" action="SelectorColor" value="white"/>
                        </components>
                      </element>
                      <element type="UIElement" name="ColorBlack" flex="1" height="56" fontSize="24" color="70,70,100,255">
                        <components>
                          <component type="UITextComponent" text="Black" align="Center" verticalAlign="Center" wrap="false"/>
                          <component type="UIInputClicker" action="SelectorColor" value="black"/>
                        </components>
                      </element>
                    </children>
                  </element>
                </children>
              </element>
              <element type="UIStack" name="EngineRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
                <children>
                  <element type="UIElement" name="EngineCaption" width="150" height="56" fontSize="24">
                    <components><component type="UITextComponent" text="Engine" verticalAlign="Center"/></components>
                  </element>
                  <element type="UISegmented" name="Engine" bind="opponentEngine" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255">
                    <children>
                      <element type="UIElement" name="EngineStockfish" flex="1" height="56" fontSize="24" color="70,70,100,255">
                        <components>
                          <component type="UITextComponent" text="Stockfish" align="Center" verticalAlign="Center" wrap="false"/>
                          <component type="UIInputClicker" action="SelectorEngine" value="stockfish"/>
                        </components>
                      </element>
                      <element type="UIElement" name="EngineOwn" flex="1" height="56" fontSize="24" color="70,70,100,255">
                        <components>
                          <component type="UITextComponent" text="Own engine" align="Center" verticalAlign="Center" wrap="false"/>
                          <component type="UIInputClicker" action="SelectorEngine" value="own"/>
                        </components>
                      </element>
                    </children>
                  </element>
                </children>
              </element>
              <element type="UIStack" name="LevelRow" height="56" direction="Horizontal" spacing="8" crossAlign="Stretch">
                <children>
                  <element type="UIElement" name="LevelCaption" width="150" height="56" fontSize="24">
                    <components><component type="UITextComponent" text="Level" verticalAlign="Center"/></components>
                  </element>
                  <element type="UISegmented" name="Level" bind="opponentLevel" flex="1" height="56" direction="Horizontal" spacing="4" color="30,30,30,255">
                    <children>
                      <element type="UIElement" name="LevelEasy" flex="1" height="56" fontSize="24" color="70,70,100,255">
                        <components>
                          <component type="UITextComponent" text="Easy" align="Center" verticalAlign="Center" wrap="false"/>
                          <component type="UIInputClicker" action="SelectorLevel" value="easy"/>
                        </components>
                      </element>
                      <element type="UIElement" name="LevelMedium" flex="1" height="56" fontSize="24" color="70,70,100,255">
                        <components>
                          <component type="UITextComponent" text="Medium" align="Center" verticalAlign="Center" wrap="false"/>
                          <component type="UIInputClicker" action="SelectorLevel" value="medium"/>
                        </components>
                      </element>
                      <element type="UIElement" name="LevelHard" flex="1" height="56" fontSize="24" color="70,70,100,255">
                        <components>
                          <component type="UITextComponent" text="Hard" align="Center" verticalAlign="Center" wrap="false"/>
                          <component type="UIInputClicker" action="SelectorLevel" value="hard"/>
                        </components>
                      </element>
                      <element type="UIElement" name="LevelMax" flex="1" height="56" fontSize="24" color="70,70,100,255">
                        <components>
                          <component type="UITextComponent" text="Max" align="Center" verticalAlign="Center" wrap="false"/>
                          <component type="UIInputClicker" action="SelectorLevel" value="max"/>
                        </components>
                      </element>
                    </children>
                  </element>
                </children>
              </element>
              <element type="UIElement" name="Ok" height="56" fontSize="26" color="40,90,60,255">
                <components>
                  <component type="UITextComponent" text="OK" align="Center" verticalAlign="Center" wrap="false"/>
                  <component type="UIInputClicker" action="SelectorClose"/>
                </components>
              </element>
            </children>
          </element>
        </elements>
        <variants>
          <variant when="orientation:portrait">
            <overrides>
              <set element="Plate" height="576"/>
              <set element="ModeRow" direction="Vertical" height="96"/>
              <set element="ModeCaption" height="32"/>
              <set element="ColorRow" direction="Vertical" height="96"/>
              <set element="ColorCaption" height="32"/>
              <set element="EngineRow" direction="Vertical" height="96"/>
              <set element="EngineCaption" height="32"/>
              <set element="LevelRow" direction="Vertical" height="96"/>
              <set element="LevelCaption" height="32"/>
              <set element="LevelEasy" fontSize="22"/>
              <set element="LevelMedium" fontSize="22"/>
              <set element="LevelHard" fontSize="22"/>
              <set element="LevelMax" fontSize="22"/>
            </overrides>
          </variant>
        </variants>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        struct Group {
            const char* name;
            const char* variable;
            eastl::vector<eastl::pair<const char*, const char*>> segments;  // the name of the button, its value
        };

        /// The four groups of the panel with the words they show (the text is in the XML above; the values are what the game publishes)
        const eastl::vector<Group>& Groups() {
            static const eastl::vector<Group> groups = {
                {"Mode", "opponentMode", {{"ModeTwo", "two"}, {"ModeComputer", "computer"}}},
                {"Color", "opponentColor", {{"ColorWhite", "white"}, {"ColorBlack", "black"}}},
                {"Engine", "opponentEngine", {{"EngineStockfish", "stockfish"}, {"EngineOwn", "own"}}},
                {"Level", "opponentLevel", {{"LevelEasy", "easy"}, {"LevelMedium", "medium"}, {"LevelHard", "hard"}, {"LevelMax", "max"}}},
            };
            return groups;
        }

        std::string SceneXml(float viewWidth, float viewHeight, const std::function<void(std::string&)>& edit = {}) {
            std::string xml = kSelectorXml;
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

        /// The part of the game: it shows and hides the panel and answers a click on a segment by setting the variable the group is bound to. It also records every action
        class SelectorGame : public SubscriptionHolder {
        public:
            explicit SelectorGame(Scene& scene) : _scene(scene) {
                Subscribe<InputEvents::ActionEvent, &SelectorGame::OnAction>(this);
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
                if (event.action == "SelectorOpen"_intern) {
                    UICanvasComponent::SetVisibleByName(_scene, "Selector"_intern, true);
                } else if (event.action == "SelectorClose"_intern) {
                    UICanvasComponent::SetVisibleByName(_scene, "Selector"_intern, false);
                } else if (event.action == "SelectorMode"_intern) {
                    Answer("opponentMode", event.value);
                } else if (event.action == "SelectorColor"_intern) {
                    Answer("opponentColor", event.value);
                } else if (event.action == "SelectorEngine"_intern) {
                    Answer("opponentEngine", event.value);
                } else if (event.action == "SelectorLevel"_intern) {
                    Answer("opponentLevel", event.value);
                }
            }

            static void Answer(const char* variable, PoolString value) {
                CoreManager::GetUIVars().Set(PoolString::Intern(variable), value.ToStringView());
            }

            Scene& _scene;
            eastl::vector<PoolString> _actions;
            eastl::vector<PoolString> _values;
        };

        /// Every control with text in the tree, depth first (the root itself is the canvas and has none)
        void CollectControls(const UIElement& element, eastl::vector<const UIElement*>& found) {
            if (element.GetComponent<UITextComponent>()) {
                found.push_back(&element);
            }
            for (const auto& child : element.children) {
                CollectControls(*child, found);
            }
        }

        eastl::string ShownText(const UIElement& element) {
            const UITextComponent* text = element.GetComponent<UITextComponent>();
            return text ? eastl::string(text->GetDisplayText()) : eastl::string();
        }

        /// What is wrong with a laid-out panel, one problem to a line, nothing when it is right: a control that is not shown at all (an empty rect: a segment without a
        /// `height`), text that its control cuts (the validator does not see it for an ellipsis label), and a segment under kSegmentHeight
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
                const float air = control->IsInteractive() ? kButtonAir : 0.0f;
                const UIContentFit fit = control->CheckContent(control->rect.w - 2.0f * air, control->rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    problems += Format("{} cuts '{}': {:.1f} units wide with {:.1f} of air each side, at scale {:.3f}\n", control->name.ToStringView(), ShownText(*control).c_str(), control->rect.w,
                                       air, scale);
                }
                if (control->IsInteractive() && control->rect.h < kSegmentHeight - 0.01f) {
                    problems += Format("{} is {:.1f} units high, under {:.0f}\n", control->name.ToStringView(), control->rect.h, kSegmentHeight);
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

    }  // namespace

    class UISelectorScreenTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetFontAtlasLibrary().Clear();
            _renderer = New<RecordingRenderer>();
            CoreManager::GetRendererManager().SetRenderer(_renderer);
            SetVars("two", "white", "stockfish", "medium");
        }

        void TearDown() override {
            _game.reset();
            _hud.Reset();
            _selector.Reset();
            _manager.Reset();
            CoreManager::GetUIVars().Clear();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        static void SetVars(const char* mode, const char* color, const char* engine, const char* level) {
            CoreManager::GetUIVars().Set("opponentMode"_intern, mode);
            CoreManager::GetUIVars().Set("opponentColor"_intern, color);
            CoreManager::GetUIVars().Set("opponentEngine"_intern, engine);
            CoreManager::GetUIVars().Set("opponentLevel"_intern, level);
        }

        /// The scene of the panel with a view of this size; the output has the same size, so a point of the view is a pixel
        void LoadScene(float viewWidth = kViewWidth, float viewHeight = kViewHeight, const std::function<void(std::string&)>& edit = {}) {
            _game.reset();
            _hud.Reset();
            _selector.Reset();
            XmlDeserializer deserializer;
            const std::string xml = SceneXml(viewWidth, viewHeight, edit);
            ASSERT_TRUE(deserializer.LoadFromString(FromStd(xml)));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _renderer->width = viewWidth;
            _renderer->height = viewHeight;

            for (const auto& node : GetScene().GetRootNode().GetChildren()) {
                if (node->GetName() == "Hud"_intern) {
                    _hud = node->GetComponent<UICanvasComponent>();
                } else if (node->GetName() == "Selector"_intern) {
                    _selector = node->GetComponent<UICanvasComponent>();
                }
            }
            ASSERT_TRUE(_hud);
            ASSERT_TRUE(_selector);
            _hud->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
            _selector->UpdateLayout(1280.0f, 720.0f);
            _game.emplace(GetScene());
        }

        Scene& GetScene() const {
            return *_manager->GetSceneByName("Main"_intern);
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

        UIElement& El(const char* name) const {
            UIElement* element = _selector->GetRoot().FindByName(PoolString::Intern(name));
            if (element == nullptr) {
                ADD_FAILURE() << name << " is not in the panel";
                std::abort();  // a reference to nothing cannot be returned; the failure above is printed first
            }
            return *element;
        }

        template <typename T>
        T& Get(const char* name) const {
            UIElement* element = _selector->GetRoot().FindByName(PoolString::Intern(name));
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

        /// A press and its release at the center of @p element as it was laid out in the last draw; returns what the two events were taken for
        std::pair<bool, bool> ClickAt(float x, float y) {
            const bool pressed = CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));
            const bool released = CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
            return {pressed, released};
        }

        std::pair<bool, bool> Click(const UIElement& element) {
            const auto [x, y] = CenterOf(element);
            return ClickAt(x, y);
        }

        /// The panel on the whole matrix with the options of the game: the findings of the validator
        eastl::vector<UIMatrixFailure> ValidatePanel(UICanvasComponent& canvas) const {
            UILayoutValidatorOptions options;
            options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
            return ValidateOnMatrix(canvas.GetLayout(), options, ViewContext());
        }

        /// Problems() on every case of the matrix: the first lines of the first cases that have any
        eastl::string ProblemsOnMatrix(UICanvasComponent& canvas) const {
            constexpr size_t kReported = 4;
            eastl::string report;
            size_t reported = 0;
            UICanvasLayout& layout = canvas.GetLayout();
            for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
                UILayoutContext context = ViewContext();
                context.outputWidth = matrixCase.context.outputWidth;
                context.outputHeight = matrixCase.context.outputHeight;
                context.safeArea = matrixCase.context.safeArea;
                layout.Update(context);
                const eastl::string problems = Problems(layout);
                if (!problems.empty() && reported++ < kReported) {
                    report += Format("{}:\n{}", matrixCase.label.c_str(), problems.c_str());
                }
            }
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

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _hud;
        IntrusivePtr<UICanvasComponent> _selector;
        Optional<SelectorGame> _game;
    };

    // ---- the size matrix --------------------------------------------------------------------------------------------------

    TEST_F(UISelectorScreenTest, ThePanelPassesTheSizeMatrixInALandscapeView) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        const auto failures = ValidatePanel(*_selector);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
        const auto hudFailures = ValidatePanel(*_hud);
        EXPECT_TRUE(hudFailures.empty()) << DescribeMatrixFailures(hudFailures).c_str();
    }

    TEST_F(UISelectorScreenTest, ThePanelPassesTheSizeMatrixInAPortraitView) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kPortraitWidth, kPortraitHeight));
        const auto failures = ValidatePanel(*_selector);
        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(UISelectorScreenTest, NoWordOfAnyGroupIsCutOnAnyCaseOfTheMatrix) {
        // The validator does not see an ellipsis, so every control is asked for the text it shows, and a segment must keep 8 units of air each side
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        const eastl::string landscape = ProblemsOnMatrix(*_selector);
        EXPECT_TRUE(landscape.empty()) << landscape.c_str();

        ASSERT_NO_FATAL_FAILURE(LoadScene(kPortraitWidth, kPortraitHeight));
        const eastl::string portrait = ProblemsOnMatrix(*_selector);
        EXPECT_TRUE(portrait.empty()) << portrait.c_str();
    }

    TEST_F(UISelectorScreenTest, NoWordIsCutInTheNarrowestWindowAndEverySegmentIsATouchTarget) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        UICanvasLayout& layout = _selector->GetLayout();
        LayOutInTheNarrowestWindow(layout);

        const float scale = kNarrowestWidth / kViewWidth;
        EXPECT_NEAR(layout.GetResolved().scale, scale, 0.01f);
        EXPECT_NEAR(layout.root->rect.w, kNarrowestWidth * (1.0f - 2.0f * UIPreview::kSideInsetShare) / scale, 0.5f) << "the safe area, in view units: about 689";

        EXPECT_TRUE(Problems(layout).empty()) << Problems(layout).c_str();
        const float minTouch = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        size_t segments = 0;
        for (const Group& group : Groups()) {
            for (const auto& [name, value] : group.segments) {
                const UIElement& segment = El(name);
                ++segments;
                EXPECT_GE(segment.rect.h, kSegmentHeight - 0.01f) << name;
                EXPECT_GE(std::min(segment.rect.w, segment.rect.h) * layout.GetResolved().scale, minTouch - 0.01f) << name << " is a touch target on the smallest window";
            }
        }
        EXPECT_EQ(segments, 10u) << "two + two + two + four";
        layout.Invalidate();
    }

    TEST_F(UISelectorScreenTest, EveryWordIsAskedNotOnlyTheLongestString) {
        // The same walk as Problems(), written out: each segment of each group, with the width it got on the narrowest window
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        UICanvasLayout& layout = _selector->GetLayout();
        LayOutInTheNarrowestWindow(layout);
        const float scale = layout.GetResolved().scale;
        for (const Group& group : Groups()) {
            for (const auto& [name, value] : group.segments) {
                const UIElement& segment = El(name);
                const UIContentFit fit = segment.CheckContent(segment.rect.w - 2.0f * kButtonAir, segment.rect.h, scale);
                EXPECT_TRUE(fit.hasContent) << name;
                EXPECT_TRUE(fit.fits) << name << " '" << ShownText(segment).c_str() << "' in " << segment.rect.w << " units";
            }
        }
        layout.Invalidate();
    }

    // ---- the shape: caption beside the group, or above it -----------------------------------------------------------------

    TEST_F(UISelectorScreenTest, InALandscapeViewTheCaptionIsBesideItsGroup) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        Draw();
        for (const char* row : {"Mode", "Color", "Engine", "Level"}) {
            const UIElement& caption = El((std::string(row) + "Caption").c_str());
            const UIElement& group = Get<UISegmented>(row);
            EXPECT_LT(caption.rect.x + caption.rect.w, group.rect.x + 0.01f) << row;
            EXPECT_NEAR(caption.rect.y, group.rect.y, 0.01f) << row;
            EXPECT_NEAR(group.rect.h, kSegmentHeight, 0.01f) << row;
        }
        const UIElement& plate = Get<UIStack>("Plate");
        EXPECT_LE(plate.rect.w, 640.01f);
        EXPECT_NEAR(plate.rect.h, 416.0f, 0.01f);
    }

    TEST_F(UISelectorScreenTest, InAPortraitViewTheCaptionIsAboveItsGroupAndTheGroupKeepsItsHeight) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kPortraitWidth, kPortraitHeight));
        Draw();
        for (const char* row : {"Mode", "Color", "Engine", "Level"}) {
            const UIElement& caption = El((std::string(row) + "Caption").c_str());
            const UIElement& group = Get<UISegmented>(row);
            EXPECT_LT(caption.rect.y + caption.rect.h, group.rect.y + 0.01f) << row;
            EXPECT_NEAR(caption.rect.x, group.rect.x, 0.01f) << row;
            EXPECT_NEAR(caption.rect.w, group.rect.w, 0.01f) << row << ": the caption takes the width of the row";
            EXPECT_NEAR(group.rect.h, kSegmentHeight, 0.01f) << row << ": the segments stay touch targets";
        }
        const UIElement& plate = Get<UIStack>("Plate");
        EXPECT_NEAR(plate.rect.h, 576.0f, 0.01f);
        EXPECT_GE(plate.rect.y, 0.0f);
        EXPECT_LE(plate.rect.y + plate.rect.h, kPortraitHeight);
    }

    TEST_F(UISelectorScreenTest, TheSegmentsOfAGroupShareItsWidth) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        Draw();
        const UIElement& level = Get<UISegmented>("Level");
        float left = level.rect.x;
        float width = -1.0f;
        for (const auto& [name, value] : Groups().back().segments) {
            const UIElement& segment = El(name);
            EXPECT_NEAR(segment.rect.x, left, 0.01f) << name;
            if (width < 0.0f) {
                width = segment.rect.w;
            }
            EXPECT_NEAR(segment.rect.w, width, 0.01f) << name << ": equal shares by flex";
            left = segment.rect.x + segment.rect.w + 4.0f;
        }
        EXPECT_NEAR(left - 4.0f, level.rect.x + level.rect.w, 0.01f) << "the four segments fill the group";
    }

    // ---- the selection comes from the variables ----------------------------------------------------------------------------

    TEST_F(UISelectorScreenTest, OneSegmentOfEachGroupIsSelectedFromTheVariables) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        SetVars("computer", "black", "own", "hard");
        ASSERT_TRUE(UICanvasComponent::SetVisibleByName(GetScene(), "Selector"_intern, true));  // a hidden canvas is not drawn, so its groups are synced on the first draw once shown
        Draw();
        const char* const chosen[] = {"ModeComputer", "ColorBlack", "EngineOwn", "LevelHard"};
        size_t selected = 0;
        for (const Group& group : Groups()) {
            for (const auto& [name, value] : group.segments) {
                const bool expected = std::any_of(std::begin(chosen), std::end(chosen), [&](const char* c) { return std::string(c) == name; });
                EXPECT_EQ(El(name).selected, expected) << name;
                selected += El(name).selected ? 1u : 0u;
            }
            EXPECT_EQ(CoreManager::GetUIVars().Get(PoolString::Intern(group.variable)), Get<UISegmented>(group.name).GetSelectedValue()) << group.name;
        }
        EXPECT_EQ(selected, 4u) << "exactly one per group";
    }

    TEST_F(UISelectorScreenTest, ThePanelHasAHiddenCanvasAtFirstAndDrawsNothingUntilShown) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        EXPECT_FALSE(_selector->_visible);
        EXPECT_TRUE(_selector->_modal);
        EXPECT_FALSE(_hud->_modal);
        Draw();
        const auto plateDrawn = [this] {
            return std::any_of(_renderer->calls.begin(), _renderer->calls.end(), [](const DrawCall& call) { return call.kind == DrawKind::Rect && call.color.r == 36 && call.color.g == 40; });
        };
        EXPECT_FALSE(plateDrawn()) << "hidden: nothing of it is drawn";

        EXPECT_TRUE(UICanvasComponent::SetVisibleByName(GetScene(), "Selector"_intern, true));
        Draw();
        EXPECT_TRUE(plateDrawn());

        EXPECT_TRUE(UICanvasComponent::SetVisibleByName(GetScene(), "Selector"_intern, false));
        Draw();
        EXPECT_FALSE(plateDrawn());
    }

    // ---- input: hidden, shown (modal), the clicks --------------------------------------------------------------------------

    TEST_F(UISelectorScreenTest, WhileHiddenThePanelTakesNoClickAndTheBoardUnderItDoes) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        // Show it once so that the segment has a place, then hide it again: the same point must now belong to the board
        ASSERT_TRUE(UICanvasComponent::SetVisibleByName(GetScene(), "Selector"_intern, true));
        Draw();
        const auto [x, y] = CenterOf(El("LevelMedium"));
        ASSERT_GT(x, 0.0f);
        ASSERT_TRUE(UICanvasComponent::SetVisibleByName(GetScene(), "Selector"_intern, false));
        Draw();
        BoardClicks board;

        const auto [pressed, released] = ClickAt(x, y);
        EXPECT_FALSE(pressed);
        EXPECT_FALSE(released);
        EXPECT_EQ(board.downs, 1) << "the game under the hidden panel got the press";
        EXPECT_EQ(_game->Total(), 0u);
    }

    TEST_F(UISelectorScreenTest, WhileShownThePanelTakesEveryPressAndTheBoardSeesNone) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_TRUE(UICanvasComponent::SetVisibleByName(GetScene(), "Selector"_intern, true));
        Draw();
        BoardClicks board;

        // On a segment, on the plate between two controls and on the dim far from the plate: all are the panel's
        const UIElement& plate = Get<UIStack>("Plate");
        const std::pair<float, float> points[] = {CenterOf(El("LevelHard")), {plate.rect.x + 4.0f, plate.rect.y + 4.0f}, {5.0f, 5.0f}, {kViewWidth - 5.0f, kViewHeight - 5.0f}};
        for (const auto& [x, y] : points) {
            const auto [pressed, released] = ClickAt(x, y);
            EXPECT_TRUE(pressed) << x << "," << y;
            EXPECT_TRUE(released) << x << "," << y;
        }
        EXPECT_EQ(board.downs, 0) << "a modal canvas keeps the press from the board";
        EXPECT_EQ(_game->Total(), 1u) << "only the click on the segment is an action; the others hit the plate or the dim";
    }

    TEST_F(UISelectorScreenTest, TheMenuButtonOpensThePanelAndOkClosesIt) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        Draw();
        BoardClicks board;
        const UIElement* newGame = _hud->GetRoot().FindByName("NewGame"_intern);
        ASSERT_NE(newGame, nullptr);
        ASSERT_FALSE(newGame->rect.IsEmpty());

        ClickAt(CenterOf(*newGame).first, CenterOf(*newGame).second);
        EXPECT_EQ(_game->LastAction(), "SelectorOpen"_intern);
        EXPECT_TRUE(_selector->_visible);
        Draw();

        // The menu button is under the dim now: the panel takes the press first
        const size_t actions = _game->Total();
        ClickAt(CenterOf(*newGame).first, CenterOf(*newGame).second);
        EXPECT_EQ(_game->Total(), actions) << "the modal panel is in front of the menu button";
        EXPECT_EQ(board.downs, 0);

        Click(El("Ok"));
        EXPECT_EQ(_game->LastAction(), "SelectorClose"_intern);
        EXPECT_FALSE(_selector->_visible);
        Draw();

        // Closed: the board gets its presses again, and the menu button works
        const auto [pressed, released] = ClickAt(kViewWidth * 0.5f, kViewHeight * 0.5f);
        EXPECT_FALSE(pressed);
        EXPECT_EQ(board.downs, 1);
        ClickAt(CenterOf(*newGame).first, CenterOf(*newGame).second);
        EXPECT_EQ(_game->LastAction(), "SelectorOpen"_intern);
    }

    TEST_F(UISelectorScreenTest, EverySegmentEmitsItsActionAndValueAndTheGameMovesTheSelection) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_TRUE(UICanvasComponent::SetVisibleByName(GetScene(), "Selector"_intern, true));
        Draw();

        const struct {
            const char* action;
            size_t group;
        } actions[] = {{"SelectorMode", 0}, {"SelectorColor", 1}, {"SelectorEngine", 2}, {"SelectorLevel", 3}};
        for (const Group& group : Groups()) {
            const size_t index = static_cast<size_t>(&group - Groups().data());
            for (const auto& [name, value] : group.segments) {
                const size_t before = _game->Total();
                const auto [pressed, released] = Click(El(name));
                EXPECT_TRUE(pressed && released) << name;
                ASSERT_EQ(_game->Total(), before + 1) << name;
                EXPECT_EQ(_game->LastAction(), PoolString::Intern(actions[index].action)) << name;
                EXPECT_EQ(_game->LastValue(), PoolString::Intern(value)) << name;

                Draw();  // the game answered with the variable; the next draw moves the selection
                EXPECT_TRUE(El(name).selected) << name;
                for (const auto& [other, otherValue] : group.segments) {
                    if (std::string(other) != name) {
                        EXPECT_FALSE(El(other).selected) << other << " after " << name;
                    }
                }
            }
        }
    }

    TEST_F(UISelectorScreenTest, AClickChangesNothingByItselfWhenTheGameDoesNotAnswer) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        ASSERT_TRUE(UICanvasComponent::SetVisibleByName(GetScene(), "Selector"_intern, true));
        _game.reset();  // nobody answers: the panel only reports
        CoreManager::GetUIVars().Set("opponentLevel"_intern, "easy");
        Draw();
        ASSERT_TRUE(El("LevelEasy").selected);

        const auto [pressed, released] = Click(El("LevelMax"));
        EXPECT_TRUE(pressed && released);
        Draw();
        EXPECT_TRUE(El("LevelEasy").selected);
        EXPECT_FALSE(El("LevelMax").selected);
        EXPECT_EQ(CoreManager::GetUIVars().Get("opponentLevel"_intern), "easy");
    }

    // ---- a deliberately broken copy must be caught ------------------------------------------------------------------------

    TEST_F(UISelectorScreenTest, ASegmentTooNarrowForItsWordIsCaughtByTheValidatorAndByTheFitCheck) {
        // "Own engine" at font 44 cannot fit its half of a row on any case: with `overflow="Clip"` the validator reports TextDoesNotFit
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](std::string& xml) {
            Replace(xml, R"(name="EngineOwn" flex="1" height="56" fontSize="24")", R"(name="EngineOwn" flex="1" height="56" fontSize="44")");
            Replace(xml, R"(text="Own engine")", R"(text="Own engine" overflow="Clip")");
        }));
        const auto failures = ValidatePanel(*_selector);
        EXPECT_FALSE(failures.empty());
        EXPECT_TRUE(HasIssue(failures, UILayoutIssueKind::TextDoesNotFit)) << DescribeMatrixFailures(failures).c_str();

        // With the default ellipsis the validator is silent ("can shrink"), and the fit check of this file is what catches it
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight,
                                          [](std::string& xml) { Replace(xml, R"(name="EngineOwn" flex="1" height="56" fontSize="24")", R"(name="EngineOwn" flex="1" height="56" fontSize="44")"); }));
        EXPECT_FALSE(HasIssue(ValidatePanel(*_selector), UILayoutIssueKind::TextDoesNotFit)) << "an ellipsis label can shrink: the validator does not report it";
        const eastl::string problems = ProblemsOnMatrix(*_selector);
        EXPECT_NE(problems.find("EngineOwn cuts 'Own engine'"), eastl::string::npos) << problems.c_str();
    }

    TEST_F(UISelectorScreenTest, ALongerWordInTheNarrowestWindowIsCaught) {
        // A word that fits at 689 units wide but not in a quarter of the Level row: the narrowest window alone shows it
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](std::string& xml) { Replace(xml, R"(text="Medium")", R"(text="Intermediate")"); }));
        UICanvasLayout& layout = _selector->GetLayout();
        LayOutInTheNarrowestWindow(layout);
        const eastl::string problems = Problems(layout);
        EXPECT_NE(problems.find("LevelMedium cuts 'Intermediate'"), eastl::string::npos) << problems.c_str();
        layout.Invalidate();
    }

    TEST_F(UISelectorScreenTest, ASegmentUnderTheTouchSizeIsCaughtByTheValidator) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](std::string& xml) { Replace(xml, R"(name="LevelMax" flex="1" height="56")", R"(name="LevelMax" flex="1" height="40")"); }));
        const auto failures = ValidatePanel(*_selector);
        EXPECT_TRUE(HasIssue(failures, UILayoutIssueKind::TouchTargetTooSmall)) << DescribeMatrixFailures(failures).c_str();
        EXPECT_NE(ProblemsOnMatrix(*_selector).find("LevelMax is 40.0 units high"), eastl::string::npos);
    }

    TEST_F(UISelectorScreenTest, ASegmentWithoutAnExplicitHeightHasNoRectAndIsCaughtHere) {
        // The gotcha of the widget: flex shares the width, not the height, so a segment without `height` gets a rect of height 0. It is not drawn, a click cannot find it
        // (`ui_click` answers "hidden") and the validator skips an empty rect, so only Problems() sees it
        ASSERT_NO_FATAL_FAILURE(LoadScene(kViewWidth, kViewHeight, [](std::string& xml) { Replace(xml, R"(name="LevelHard" flex="1" height="56")", R"(name="LevelHard" flex="1")"); }));
        ASSERT_TRUE(UICanvasComponent::SetVisibleByName(GetScene(), "Selector"_intern, true));
        Draw();
        EXPECT_NEAR(El("LevelHard").rect.h, 0.0f, 0.01f);
        UICanvasLayout& layout = _selector->GetLayout();
        const eastl::string problems = Problems(layout);
        EXPECT_NE(problems.find("LevelHard 'Hard' has an empty rect"), eastl::string::npos) << problems.c_str();
    }

}  // namespace BECore::Tests
