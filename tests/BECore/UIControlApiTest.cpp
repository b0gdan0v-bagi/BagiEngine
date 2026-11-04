#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIControl.h>
#include <BECore/UI/UISegmented.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-2f;

        // "Main": output 800 x 600, every canvas in window pixels (scale 1).
        //   Hud (layer 10): Plate (10, 10, 200 x 100) holds Title (a label with a variable) and Ok (a button, 122, 62, 80 x 40); Off (300, 200, 80 x 40) is a disabled
        //   button; Under (500, 300, 80 x 40) is a button with Cover (490, 290, 100 x 60) drawn over it; Note (300, 300, 80 x 40) is a label.
        //   Debug (layer 5): Tiny (400, 10, 20 x 20) is a button below the touch size, and a second Ok (500, 100, 80 x 40) with another action.
        //   Menu (layer 20): a modal canvas, hidden at first.
        // "Fit": 400 x 300 units, shown in a 1000 x 600 output: scale 2, offset (100, 0). View is a SceneView canvas with the button Go (10, 10, 50 x 40).
        // "Choice": 800 x 600 in window pixels. Panel holds Side, a UISegmented bound to `uiApiSide` with the buttons Left and Right (the same action, values "left" and "right"; 150 x 56
        //   and 150 x 56 at x = 100, 252, y = 100), and Plain (100, 300, 80 x 40) is a button without a value.
        // "Empty": a scene without canvases.
        constexpr const char* kScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIPanel" name="Plate" x="10" y="10" width="200" height="100" color="10,20,30,200">
            <children>
              <element type="UILabel" name="Title" x="8" y="8" width="184" height="30" text="Score: {score}" fontSize="24"/>
              <element type="UIButton" name="Ok" anchor="BottomRight" x="-8" y="-8" width="80" height="40" text="OK" action="UIControlApiOk" color="40,90,60,255"/>
            </children>
          </element>
          <element type="UIButton" name="Off" x="300" y="200" width="80" height="40" text="Off" action="UIControlApiOff" color="40,90,60,255" disabled="true"/>
          <element type="UILabel" name="Note" x="300" y="300" width="80" height="40" text="Note"/>
          <element type="UIButton" name="Under" x="500" y="300" width="80" height="40" text="Under" action="UIControlApiUnder" color="40,90,60,255"/>
          <element type="UIPanel" name="Cover" x="490" y="290" width="100" height="60" blocksInput="true"/>
        </elements>
      </component>
    </components></node>
    <node name="Debug" layer="5"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UIButton" name="Tiny" x="400" y="10" width="20" height="20" text="T" action="UIControlApiTiny" color="40,90,60,255"/>
          <element type="UIButton" name="Ok" x="500" y="100" width="80" height="40" text="OK" action="UIControlApiDebugOk" color="40,90,60,255"/>
        </elements>
      </component>
    </components></node>
    <node name="Menu" layer="20"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize" modal="true" visible="false"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Fit" viewWidth="400" viewHeight="300"><nodes>
    <node name="View"><components>
      <component type="UICanvasComponent" space="SceneView">
        <elements>
          <element type="UIButton" name="Go" x="10" y="10" width="50" height="40" text="Go" action="UIControlApiGo" color="40,90,60,255"/>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Choice"><nodes>
    <node name="Panel"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
        <elements>
          <element type="UISegmented" name="Side" bind="uiApiSide" x="100" y="100" width="302" height="56" direction="Horizontal" spacing="2" color="30,30,30,255">
            <children>
              <element type="UIButton" name="Left" flex="1" height="56" text="Left" value="left" action="UIControlApiSide" color="70,70,100,255"/>
              <element type="UIButton" name="Right" flex="1" height="56" text="Right" value="right" action="UIControlApiSide" color="70,70,100,255"/>
            </children>
          </element>
          <element type="UIButton" name="Plain" x="100" y="300" width="80" height="40" text="Plain" action="UIControlApiPlain" color="40,90,60,255"/>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Empty"><nodes/></scene>
</scenes></SceneManager>)";

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        /// Counts the actions the buttons emit and the mouse events that reach game code
        class Recorder : public SubscriptionHolder {
        public:
            Recorder() {
                Subscribe<InputEvents::ActionEvent, &Recorder::OnAction>(this);
                Subscribe<SDLEvents::MouseButtonDownEvent, &Recorder::OnDown>(this);
                Subscribe<SDLEvents::MouseButtonUpEvent, &Recorder::OnUp>(this);
            }

            int Count(const char* action) const {
                return static_cast<int>(std::count(_actions.begin(), _actions.end(), PoolString::Intern(action)));
            }
            size_t Total() const {
                return _actions.size();
            }
            /// The value of the last action ("<none>" when there was none)
            std::string LastValue() const {
                return _values.empty() ? std::string("<none>") : std::string(_values.back().ToStringView().data(), _values.back().ToStringView().size());
            }

            int downs = 0;
            int ups = 0;

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                _actions.push_back(event.action);
                _values.push_back(event.value);
            }
            void OnDown(const SDLEvents::MouseButtonDownEvent&) {
                ++downs;
            }
            void OnUp(const SDLEvents::MouseButtonUpEvent&) {
                ++ups;
            }

            eastl::vector<PoolString> _actions;
            eastl::vector<PoolString> _values;
        };

        std::string Text(const eastl::string& text) {
            return std::string(text.c_str(), text.size());
        }

        std::string StringMember(const Json& object, eastl::string_view key) {
            const auto value = GetJsonString(object, key);
            return value ? Text(*value) : std::string("<missing>");
        }

        double NumberMember(const Json& object, eastl::string_view key) {
            return GetJsonNumber(object, key).value_or(-12345.0);
        }

        bool BoolMember(const Json& object, eastl::string_view key) {
            return GetJsonBool(object, key).value_or(false);
        }

        const Json* Member(const Json& object, eastl::string_view key) {
            return FindJsonMember(object, key);
        }

        void ExpectRect(const Json* rect, float x, float y, float w, float h) {
            ASSERT_NE(rect, nullptr);
            EXPECT_NEAR(NumberMember(*rect, "x"), x, kTolerance);
            EXPECT_NEAR(NumberMember(*rect, "y"), y, kTolerance);
            EXPECT_NEAR(NumberMember(*rect, "w"), w, kTolerance);
            EXPECT_NEAR(NumberMember(*rect, "h"), h, kTolerance);
        }

    }  // namespace

    class UIControlApiTest : public ::testing::Test {
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
            ASSERT_TRUE(deserializer.LoadFromString(kScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);
            RegisterUIControl(_service);
        }

        void TearDown() override {
            CoreManager::GetUIVars().Clear();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        /// Shows the scene and draws a frame
        void Show(const char* sceneName, float width = 800.0f, float height = 600.0f) {
            _renderer->width = width;
            _renderer->height = height;
            _manager->SetActiveScene(PoolString::Intern(sceneName));
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        IntrusivePtr<UICanvasComponent> Canvas(const char* sceneName, const char* nodeName) const {
            for (const auto& node : _manager->GetSceneByName(PoolString::Intern(sceneName))->GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(nodeName)) {
                    return node->GetComponent<UICanvasComponent>();
                }
            }
            return {};
        }

        /// Sends a request, runs the frame stage that answers it and returns the whole response
        Json Call(std::string_view method, const Json& params = Json()) {
            Json request = Json::object();
            request["id"] = ++_nextId;
            request["method"] = std::string(method);
            if (!params.is_null()) {
                request["params"] = params;
            }
            Json response;
            _service.Post(DumpJson(request), [&response](eastl::string text) {
                auto parsed = ParseJson(text);
                response = parsed ? std::move(*parsed) : Json();
            });
            _service.Drain();
            return response;
        }

        static Json Click(const char* name) {
            Json params = Json::object();
            params["name"] = std::string(name);
            return params;
        }

        static bool IsOk(const Json& response) {
            return BoolMember(response, "ok");
        }

        static std::string ErrorCode(const Json& response) {
            const Json* error = Member(response, "error");
            return error != nullptr ? StringMember(*error, "code") : std::string("<no error>");
        }

        static std::string ErrorMessage(const Json& response) {
            const Json* error = Member(response, "error");
            return error != nullptr ? StringMember(*error, "message") : std::string("<no error>");
        }

        /// The canvas of the result named after a scene node
        static const Json* FindCanvas(const Json& result, std::string_view node) {
            const Json* canvases = Member(result, "canvases");
            if (canvases == nullptr || !canvases->is_array()) {
                return nullptr;
            }
            for (const Json& canvas : *canvases) {
                if (StringMember(canvas, "node") == node) {
                    return &canvas;
                }
            }
            return nullptr;
        }

        /// The element of a canvas, searched through the whole tree
        static const Json* FindElement(const Json& parent, std::string_view name) {
            const Json* list = Member(parent, parent.contains("elements") ? "elements" : "children");
            if (list == nullptr || !list->is_array()) {
                return nullptr;
            }
            for (const Json& element : *list) {
                if (StringMember(element, "name") == name) {
                    return &element;
                }
                if (const Json* inside = FindElement(element, name)) {
                    return inside;
                }
            }
            return nullptr;
        }

        ControlService _service;
        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        int64_t _nextId = 0;
    };

    TEST_F(UIControlApiTest, TheMethodsAreRegistered) {
        const Json tree = Call("ui_tree");
        EXPECT_NE(ErrorCode(tree), "unknown_method");

        const Json click = Call("ui_click", Click("Ok"));
        EXPECT_NE(ErrorCode(click), "unknown_method");
    }

    // ---- ui_tree ----------------------------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, ASceneWithoutCanvasesHasAnEmptyList) {
        _manager->SetActiveScene(PoolString::Intern("Empty"));

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* result = Member(response, "result");
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(StringMember(*result, "scene"), "Empty");
        const Json* canvases = Member(*result, "canvases");
        ASSERT_NE(canvases, nullptr);
        EXPECT_TRUE(canvases->is_array());
        EXPECT_TRUE(canvases->empty());
    }

    TEST_F(UIControlApiTest, BothMethodsNeedAnActiveScene) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString("<SceneManager><scenes/></SceneManager>"));
        auto bare = New<SceneManager>();
        bare->Deserialize(deserializer);
        bare->Initialize();
        ASSERT_EQ(bare->GetActiveScene(), nullptr);
        EngineEnvironment::UseSceneManager(bare);

        EXPECT_EQ(ErrorCode(Call("ui_tree")), "no_scene");
        EXPECT_EQ(ErrorCode(Call("ui_click", Click("Ok"))), "no_scene");
    }

    TEST_F(UIControlApiTest, ACanvasThatWasNeverDrawnHasItsStructureAndNoRects) {
        _manager->SetActiveScene(PoolString::Intern("Main"));

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* hud = FindCanvas(*Member(response, "result"), "Hud");
        ASSERT_NE(hud, nullptr);
        EXPECT_FALSE(BoolMember(*hud, "laidOut"));
        EXPECT_EQ(Member(*hud, "bounds"), nullptr);
        const Json* ok = FindElement(*hud, "Ok");
        ASSERT_NE(ok, nullptr);
        EXPECT_EQ(StringMember(*ok, "type"), "UIButton");
        EXPECT_EQ(Member(*ok, "rect"), nullptr);
        EXPECT_EQ(Member(*ok, "visible"), nullptr);
        const Json* issues = Member(*hud, "issues");
        ASSERT_NE(issues, nullptr);
        EXPECT_TRUE(issues->empty());
    }

    TEST_F(UIControlApiTest, ATreeReportsCanvasesElementsAndTheirRects) {
        Show("Main");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json& result = *Member(response, "result");
        EXPECT_EQ(StringMember(result, "scene"), "Main");
        const Json* canvases = Member(result, "canvases");
        ASSERT_NE(canvases, nullptr);
        EXPECT_EQ(canvases->size(), 3u);

        const Json* hud = FindCanvas(result, "Hud");
        ASSERT_NE(hud, nullptr);
        EXPECT_EQ(StringMember(*hud, "space"), "Screen");
        EXPECT_TRUE(BoolMember(*hud, "visible"));
        EXPECT_FALSE(BoolMember(*hud, "modal"));
        EXPECT_TRUE(BoolMember(*hud, "inputEnabled"));
        EXPECT_TRUE(BoolMember(*hud, "takesInput"));
        EXPECT_TRUE(BoolMember(*hud, "laidOut"));
        EXPECT_NEAR(NumberMember(*hud, "scale"), 1.0, kTolerance);
        ExpectRect(Member(*hud, "bounds"), 0.0f, 0.0f, 800.0f, 600.0f);
        ExpectRect(Member(*hud, "contentBounds"), 0.0f, 0.0f, 800.0f, 600.0f);

        // Rects are in canvas coordinates, not relative to the parent
        const Json* plate = FindElement(*hud, "Plate");
        ASSERT_NE(plate, nullptr);
        EXPECT_EQ(StringMember(*plate, "type"), "UIPanel");
        ExpectRect(Member(*plate, "rect"), 10.0f, 10.0f, 200.0f, 100.0f);
        const Json* ok = FindElement(*plate, "Ok");
        ASSERT_NE(ok, nullptr);
        EXPECT_EQ(StringMember(*ok, "type"), "UIButton");
        ExpectRect(Member(*ok, "rect"), 122.0f, 62.0f, 80.0f, 40.0f);
        ExpectRect(Member(*ok, "outputRect"), 122.0f, 62.0f, 80.0f, 40.0f);
        EXPECT_TRUE(BoolMember(*ok, "visible"));
        EXPECT_TRUE(BoolMember(*ok, "interactive"));
        EXPECT_EQ(StringMember(*ok, "text"), "OK");
        EXPECT_EQ(StringMember(*ok, "action"), "UIControlApiOk");
        EXPECT_FALSE(BoolMember(*ok, "disabled"));

        const Json* off = FindElement(*hud, "Off");
        ASSERT_NE(off, nullptr);
        EXPECT_TRUE(BoolMember(*off, "disabled"));

        const Json* note = FindElement(*hud, "Note");
        ASSERT_NE(note, nullptr);
        EXPECT_EQ(StringMember(*note, "type"), "UILabel");
        EXPECT_FALSE(BoolMember(*note, "interactive"));
        EXPECT_EQ(Member(*note, "action"), nullptr);

        const Json* menu = FindCanvas(result, "Menu");
        ASSERT_NE(menu, nullptr);
        EXPECT_TRUE(BoolMember(*menu, "modal"));
        EXPECT_FALSE(BoolMember(*menu, "visible"));
        EXPECT_FALSE(BoolMember(*menu, "takesInput"));
    }

    TEST_F(UIControlApiTest, ALabelShowsItsTextWithTheVariablesReplaced) {
        CoreManager::GetUIVars().Set("score"_intern, 42);
        Show("Main");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* hud = FindCanvas(*Member(response, "result"), "Hud");
        ASSERT_NE(hud, nullptr);
        const Json* title = FindElement(*hud, "Title");
        ASSERT_NE(title, nullptr);
        EXPECT_EQ(StringMember(*title, "text"), "Score: 42");
    }

    TEST_F(UIControlApiTest, AHiddenElementIsNotVisible) {
        auto hudCanvas = Canvas("Main", "Hud");
        ASSERT_TRUE(hudCanvas->SetElementVisible("Note"_intern, false));
        Show("Main");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* hud = FindCanvas(*Member(response, "result"), "Hud");
        ASSERT_NE(hud, nullptr);
        const Json* note = FindElement(*hud, "Note");
        ASSERT_NE(note, nullptr);
        EXPECT_FALSE(BoolMember(*note, "visible"));
        const Json* rect = Member(*note, "rect");
        ASSERT_NE(rect, nullptr);
        EXPECT_TRUE(NumberMember(*rect, "w") == 0.0 || NumberMember(*rect, "h") == 0.0) << "a hidden element has an empty rect";
    }

    TEST_F(UIControlApiTest, TheValidatorsFindingsAreInTheTree) {
        Show("Main");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json& result = *Member(response, "result");
        const Json* hud = FindCanvas(result, "Hud");
        const Json* debug = FindCanvas(result, "Debug");
        ASSERT_TRUE(hud != nullptr && debug != nullptr);
        const Json* hudIssues = Member(*hud, "issues");
        ASSERT_NE(hudIssues, nullptr);
        EXPECT_TRUE(hudIssues->empty()) << DumpJson(*hudIssues).c_str();

        const Json* debugIssues = Member(*debug, "issues");
        ASSERT_NE(debugIssues, nullptr);
        ASSERT_EQ(debugIssues->size(), 1u) << DumpJson(*debugIssues).c_str();
        const Json& issue = (*debugIssues)[0];
        EXPECT_EQ(StringMember(issue, "kind"), "TouchTargetTooSmall");
        EXPECT_EQ(StringMember(issue, "element"), "Tiny");
        ExpectRect(Member(issue, "rect"), 400.0f, 10.0f, 20.0f, 20.0f);
    }

    TEST_F(UIControlApiTest, OutputRectsAreInTheOutputPixelsOfTheCanvasTransform) {
        Show("Fit", 1000.0f, 600.0f);

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* view = FindCanvas(*Member(response, "result"), "View");
        ASSERT_NE(view, nullptr);
        EXPECT_EQ(StringMember(*view, "space"), "SceneView");
        EXPECT_NEAR(NumberMember(*view, "scale"), 2.0, kTolerance);
        const Json* go = FindElement(*view, "Go");
        ASSERT_NE(go, nullptr);
        ExpectRect(Member(*go, "rect"), 10.0f, 10.0f, 50.0f, 40.0f);
        ExpectRect(Member(*go, "outputRect"), 120.0f, 20.0f, 100.0f, 80.0f);
    }

    // ---- the choice group ---------------------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, ATreeShowsTheValueAndTheSelectionOfAChoiceGroup) {
        CoreManager::GetUIVars().Set("uiApiSide"_intern, "right");
        Show("Choice");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* panel = FindCanvas(*Member(response, "result"), "Panel");
        ASSERT_NE(panel, nullptr);
        const Json* side = FindElement(*panel, "Side");
        ASSERT_NE(side, nullptr);
        EXPECT_EQ(StringMember(*side, "type"), "UISegmented");
        EXPECT_EQ(StringMember(*side, "bind"), "uiApiSide");
        EXPECT_EQ(StringMember(*side, "selectedValue"), "right");
        EXPECT_FALSE(BoolMember(*side, "disabled"));
        const Json* left = FindElement(*side, "Left");
        const Json* right = FindElement(*side, "Right");
        ASSERT_TRUE(left != nullptr && right != nullptr);
        EXPECT_EQ(StringMember(*left, "value"), "left");
        EXPECT_EQ(StringMember(*right, "value"), "right");
        EXPECT_EQ(StringMember(*left, "action"), "UIControlApiSide");
        EXPECT_FALSE(BoolMember(*left, "selected"));
        EXPECT_TRUE(BoolMember(*right, "selected"));
        ExpectRect(Member(*left, "rect"), 100.0f, 100.0f, 150.0f, 56.0f);
        ExpectRect(Member(*right, "rect"), 252.0f, 100.0f, 150.0f, 56.0f);
    }

    TEST_F(UIControlApiTest, TheSelectionFollowsTheVariableBetweenTwoTreeCalls) {
        CoreManager::GetUIVars().Set("uiApiSide"_intern, "left");
        Show("Choice");
        const Json first = Call("ui_tree");

        CoreManager::GetUIVars().Set("uiApiSide"_intern, "right");  // no draw in between: the tree must not show the last draw's selection
        const Json second = Call("ui_tree");

        ASSERT_TRUE(IsOk(first) && IsOk(second));
        const Json* before = FindElement(*FindCanvas(*Member(first, "result"), "Panel"), "Side");
        const Json* after = FindElement(*FindCanvas(*Member(second, "result"), "Panel"), "Side");
        ASSERT_TRUE(before != nullptr && after != nullptr);
        EXPECT_EQ(StringMember(*before, "selectedValue"), "left");
        EXPECT_TRUE(BoolMember(*FindElement(*before, "Left"), "selected"));
        EXPECT_FALSE(BoolMember(*FindElement(*before, "Right"), "selected"));
        EXPECT_EQ(StringMember(*after, "selectedValue"), "right");
        EXPECT_FALSE(BoolMember(*FindElement(*after, "Left"), "selected"));
        EXPECT_TRUE(BoolMember(*FindElement(*after, "Right"), "selected"));
    }

    TEST_F(UIControlApiTest, AnUnsetVariableSelectsNothingAndATreeBeforeTheFirstDrawStillSaysSo) {
        _manager->SetActiveScene(PoolString::Intern("Choice"));

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* side = FindElement(*FindCanvas(*Member(response, "result"), "Panel"), "Side");
        ASSERT_NE(side, nullptr);
        EXPECT_EQ(StringMember(*side, "selectedValue"), "");
        EXPECT_FALSE(BoolMember(*FindElement(*side, "Left"), "selected"));
        EXPECT_FALSE(BoolMember(*FindElement(*side, "Right"), "selected"));
    }

    TEST_F(UIControlApiTest, ADisabledGroupShowsItsSegmentsAsDisabled) {
        auto* side = Canvas("Choice", "Panel")->GetLayout().root->children[0]->Cast<UISegmented>();
        ASSERT_NE(side, nullptr);
        side->disabled = true;
        Show("Choice");

        const Json response = Call("ui_tree");

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json* group = FindElement(*FindCanvas(*Member(response, "result"), "Panel"), "Side");
        ASSERT_NE(group, nullptr);
        EXPECT_TRUE(BoolMember(*group, "disabled"));
        EXPECT_TRUE(BoolMember(*FindElement(*group, "Left"), "disabled")) << "a click on it does nothing, which is what the field says";
        EXPECT_TRUE(BoolMember(*FindElement(*group, "Right"), "disabled"));
        EXPECT_FALSE(BoolMember(*FindElement(*FindCanvas(*Member(response, "result"), "Panel"), "Plain"), "disabled"));
    }

    TEST_F(UIControlApiTest, AClickOnASegmentAnswersWithTheActionAndTheValueAndTheListenerGetsThem) {
        CoreManager::GetUIVars().Set("uiApiSide"_intern, "left");
        Show("Choice");
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("Right"));

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        const Json& result = *Member(clicked, "result");
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiSide");
        EXPECT_EQ(StringMember(result, "value"), "right");
        EXPECT_EQ(recorder.Count("UIControlApiSide"), 1);
        EXPECT_EQ(recorder.LastValue(), "right");
    }

    TEST_F(UIControlApiTest, AChoiceCanBeCheckedEndToEnd) {
        CoreManager::GetUIVars().Set("uiApiSide"_intern, "left");
        Show("Choice");
        const Json before = Call("ui_tree");
        ASSERT_TRUE(IsOk(before));
        EXPECT_TRUE(BoolMember(*FindElement(*FindCanvas(*Member(before, "result"), "Panel"), "Left"), "selected"));

        // The click only reports; the game (here the test) answers by setting the variable
        ASSERT_TRUE(IsOk(Call("ui_click", Click("Right"))));
        const Json unchanged = Call("ui_tree");
        ASSERT_TRUE(IsOk(unchanged));
        EXPECT_TRUE(BoolMember(*FindElement(*FindCanvas(*Member(unchanged, "result"), "Panel"), "Left"), "selected")) << "a click selects nothing by itself";
        EXPECT_FALSE(BoolMember(*FindElement(*FindCanvas(*Member(unchanged, "result"), "Panel"), "Right"), "selected"));

        CoreManager::GetUIVars().Set("uiApiSide"_intern, "right");
        const Json after = Call("ui_tree");
        ASSERT_TRUE(IsOk(after));
        const Json* panel = FindCanvas(*Member(after, "result"), "Panel");
        EXPECT_FALSE(BoolMember(*FindElement(*panel, "Left"), "selected"));
        EXPECT_TRUE(BoolMember(*FindElement(*panel, "Right"), "selected"));
    }

    TEST_F(UIControlApiTest, ADisabledGroupTakesTheClickAndEmitsNothing) {
        auto* side = Canvas("Choice", "Panel")->GetLayout().root->children[0]->Cast<UISegmented>();
        ASSERT_NE(side, nullptr);
        side->disabled = true;
        Show("Choice");
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("Left"));

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, APlainButtonHasAnEmptyValueAndNeverSelectedAndALabelHasNeither) {
        Show("Choice");
        Recorder recorder;

        const Json tree = Call("ui_tree");
        const Json clicked = Call("ui_click", Click("Plain"));

        ASSERT_TRUE(IsOk(tree) && IsOk(clicked));
        const Json* plain = FindElement(*FindCanvas(*Member(tree, "result"), "Panel"), "Plain");
        ASSERT_NE(plain, nullptr);
        EXPECT_NE(Member(*plain, "value"), nullptr);
        EXPECT_EQ(StringMember(*plain, "value"), "");
        EXPECT_FALSE(BoolMember(*plain, "selected"));
        EXPECT_EQ(Member(*plain, "bind"), nullptr) << "only a choice group has a bind";
        EXPECT_EQ(StringMember(*Member(clicked, "result"), "value"), "");
        EXPECT_EQ(recorder.Count("UIControlApiPlain"), 1);
        EXPECT_EQ(recorder.LastValue(), "");

        Show("Main");
        const Json main = Call("ui_tree");
        const Json* note = FindElement(*FindCanvas(*Member(main, "result"), "Hud"), "Note");
        ASSERT_NE(note, nullptr);
        EXPECT_EQ(Member(*note, "value"), nullptr);
        EXPECT_EQ(Member(*note, "selected"), nullptr);
    }

    // ---- ui_click ---------------------------------------------------------------------------------------------------------

    TEST_F(UIControlApiTest, AClickPressesAButtonLikeAUserWould) {
        Show("Main");
        Recorder recorder;

        const Json clicked = Call("ui_click", Click("Ok"));  // the Hud one: the first of the two named Ok in the scene order

        ASSERT_TRUE(IsOk(clicked)) << ErrorMessage(clicked);
        const Json& result = *Member(clicked, "result");
        EXPECT_EQ(StringMember(result, "clicked"), "Ok");
        EXPECT_EQ(StringMember(result, "canvas"), "Hud");
        EXPECT_EQ(StringMember(result, "action"), "UIControlApiOk");
        EXPECT_NEAR(NumberMember(result, "x"), 162.0, kTolerance);
        EXPECT_NEAR(NumberMember(result, "y"), 82.0, kTolerance);
        EXPECT_TRUE(BoolMember(result, "consumed"));
        EXPECT_EQ(recorder.Count("UIControlApiOk"), 1);
        EXPECT_EQ(recorder.Total(), 1u);
        EXPECT_EQ(recorder.downs, 0) << "the canvas took the press, so the game did not see it";
        EXPECT_EQ(recorder.ups, 0);
    }

    TEST_F(UIControlApiTest, TheClickPointIsInOutputPixelsOfTheCanvasTransform) {
        Show("Fit", 1000.0f, 600.0f);
        Recorder recorder;

        const Json response = Call("ui_click", Click("Go"));

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        const Json& result = *Member(response, "result");
        EXPECT_NEAR(NumberMember(result, "x"), 170.0, kTolerance);  // (10 + 25) * 2 + 100
        EXPECT_NEAR(NumberMember(result, "y"), 60.0, kTolerance);   // (10 + 20) * 2
        EXPECT_EQ(recorder.Count("UIControlApiGo"), 1);
    }

    TEST_F(UIControlApiTest, TheCanvasParameterChoosesTheElementWhenNamesRepeat) {
        Show("Main");
        Recorder recorder;
        Json params = Click("Ok");
        params["canvas"] = std::string("Debug");

        const Json response = Call("ui_click", params);

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        EXPECT_EQ(StringMember(*Member(response, "result"), "canvas"), "Debug");
        EXPECT_EQ(recorder.Count("UIControlApiDebugOk"), 1);
        EXPECT_EQ(recorder.Count("UIControlApiOk"), 0);
    }

    TEST_F(UIControlApiTest, ADisabledButtonTakesTheClickAndDoesNothingWithIt) {
        Show("Main");
        Recorder recorder;

        const Json response = Call("ui_click", Click("Off"));

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        EXPECT_TRUE(BoolMember(*Member(response, "result"), "consumed"));
        EXPECT_EQ(recorder.Total(), 0u);
        EXPECT_EQ(recorder.downs, 0);
    }

    TEST_F(UIControlApiTest, AModalCanvasAboveTakesTheClick) {
        auto menu = Canvas("Main", "Menu");
        menu->_visible = true;
        Show("Main");
        Recorder recorder;

        const Json response = Call("ui_click", Click("Ok"));

        ASSERT_TRUE(IsOk(response)) << ErrorMessage(response);
        EXPECT_TRUE(BoolMember(*Member(response, "result"), "consumed"));
        EXPECT_EQ(recorder.Total(), 0u) << "the modal canvas took the click, as it would take a user's";
        EXPECT_EQ(recorder.downs, 0);
    }

    TEST_F(UIControlApiTest, AnElementThatDoesNotExistIsNotFoundAndItsNameIsNotInterned) {
        Show("Main");
        constexpr const char* kUnknown = "ui_click_no_such_element_77c1d3";
        ASSERT_FALSE(PoolString::Find(kUnknown).has_value());

        const Json response = Call("ui_click", Click(kUnknown));

        EXPECT_FALSE(IsOk(response));
        EXPECT_EQ(ErrorCode(response), "not_found");
        EXPECT_FALSE(PoolString::Find(kUnknown).has_value()) << "text from a client must not grow the pool";
    }

    TEST_F(UIControlApiTest, ACanvasNameThatNoNodeHasIsNotFound) {
        Show("Main");
        Json params = Click("Ok");
        params["canvas"] = std::string("ui_click_no_such_node_5a90e2");

        const Json response = Call("ui_click", params);

        EXPECT_EQ(ErrorCode(response), "not_found");
        EXPECT_FALSE(PoolString::Find("ui_click_no_such_node_5a90e2").has_value());
    }

    TEST_F(UIControlApiTest, AnElementThatIsNotInteractiveCannotBeClicked) {
        Show("Main");
        Recorder recorder;

        const Json response = Call("ui_click", Click("Note"));

        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("not interactive"), std::string::npos) << ErrorMessage(response);
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AHiddenButtonCannotBeClicked) {
        auto hud = Canvas("Main", "Hud");
        ASSERT_TRUE(hud->SetElementVisible("Off"_intern, false));
        Show("Main");

        const Json response = Call("ui_click", Click("Off"));

        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("hidden"), std::string::npos) << ErrorMessage(response);
    }

    TEST_F(UIControlApiTest, AButtonCoveredByAnotherElementCannotBeClicked) {
        Show("Main");
        Recorder recorder;

        const Json response = Call("ui_click", Click("Under"));

        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("covered by 'Cover'"), std::string::npos) << ErrorMessage(response);
        EXPECT_EQ(recorder.Total(), 0u);
    }

    TEST_F(UIControlApiTest, AButtonOfACanvasThatTakesNoInputCannotBeClicked) {
        auto hud = Canvas("Main", "Hud");
        hud->_inputEnabled = false;
        Show("Main");

        const Json response = Call("ui_click", Click("Off"));

        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("canvas 'Hud'"), std::string::npos) << ErrorMessage(response);
    }

    TEST_F(UIControlApiTest, AButtonBeforeTheFirstDrawAsksForAFrame) {
        _manager->SetActiveScene(PoolString::Intern("Main"));

        const Json response = Call("ui_click", Click("Ok"));

        EXPECT_EQ(ErrorCode(response), "not_clickable");
        EXPECT_NE(ErrorMessage(response).find("step a frame"), std::string::npos) << ErrorMessage(response);
    }

    TEST_F(UIControlApiTest, TheClickHasToNameAnElement) {
        Show("Main");

        Json noName = Json::object();
        EXPECT_EQ(ErrorCode(Call("ui_click", noName)), "invalid_params");

        Json emptyName = Json::object();
        emptyName["name"] = std::string();
        EXPECT_EQ(ErrorCode(Call("ui_click", emptyName)), "invalid_params");

        Json numberName = Json::object();
        numberName["name"] = 7;
        EXPECT_EQ(ErrorCode(Call("ui_click", numberName)), "invalid_params");

        Json badCanvas = Click("Ok");
        badCanvas["canvas"] = 7;
        EXPECT_EQ(ErrorCode(Call("ui_click", badCanvas)), "invalid_params");
    }

}  // namespace BECore::Tests
