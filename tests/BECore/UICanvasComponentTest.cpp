#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 1e-3f;

        // "Main": no view size, so scene units are pixels. Hud is the lower canvas, Menu a modal one above it (hidden at first), Board a quad at layer 5.
        // "Fit": 400 x 300 units shown in an 800 x 600 output (scale 2): a SceneView canvas, a World canvas on a node at (100, 60) 80 x 20, a Screen canvas
        // that stays in pixels, and a quad on a layer above all of them.
        // "Other": a scene that is never active in these tests, with a canvas at the same place as Hud's.
        constexpr const char* kCanvasScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
    </components></node>
    <node name="Menu" layer="20"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize" modal="true" visible="false"/>
    </components></node>
    <node name="Board" layer="5"><components>
      <component type="TransformComponent" x="0" y="0" width="50" height="50"/>
      <component type="QuadRendererComponent"><color r="255" g="0" b="0" a="255"/></component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Fit" viewWidth="400" viewHeight="300"><nodes>
    <node name="ScreenCanvas"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
    </components></node>
    <node name="ViewCanvas"><components>
      <component type="UICanvasComponent" space="SceneView"/>
    </components></node>
    <node name="WorldCanvas"><components>
      <component type="TransformComponent" x="100" y="60" width="80" height="20"/>
      <component type="UICanvasComponent" space="World"/>
    </components></node>
    <node name="Quad" layer="1"><components>
      <component type="TransformComponent" x="10" y="10" width="20" height="20"/>
      <component type="QuadRendererComponent"><color r="0" g="255" b="0" a="255"/></component>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Other"><nodes>
    <node name="Hud"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        /// Counts the mouse events that reach game code
        class GameMouseRecorder : public SubscriptionHolder {
        public:
            GameMouseRecorder() {
                Subscribe<SDLEvents::MouseButtonDownEvent, &GameMouseRecorder::OnDown>(this);
                Subscribe<SDLEvents::MouseButtonUpEvent, &GameMouseRecorder::OnUp>(this);
                Subscribe<SDLEvents::MouseMotionEvent, &GameMouseRecorder::OnMotion>(this);
            }

            int downs = 0;
            int ups = 0;
            int motions = 0;

        private:
            void OnDown(const SDLEvents::MouseButtonDownEvent&) {
                ++downs;
            }
            void OnUp(const SDLEvents::MouseButtonUpEvent&) {
                ++ups;
            }
            void OnMotion(const SDLEvents::MouseMotionEvent&) {
                ++motions;
            }
        };

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        SDL_MouseMotionEvent Motion(float x, float y, SDL_MouseButtonFlags buttonsHeld = 0) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.x = x;
            event.y = y;
            event.state = buttonsHeld;
            return event;
        }

        void ExpectRect(const Rect& rect, float x, float y, float w, float h) {
            EXPECT_NEAR(rect.x, x, kTolerance);
            EXPECT_NEAR(rect.y, y, kTolerance);
            EXPECT_NEAR(rect.w, w, kTolerance);
            EXPECT_NEAR(rect.h, h, kTolerance);
        }

        /// A square element with a color that a click on it is consumed by
        IntrusivePtr<UIElement> AddSquare(UICanvasComponent& canvas, const char* name, float x, float y, float size, Color color = Color{255, 255, 255, 255}) {
            auto square = canvas.GetRoot().AddChild(PoolString::Intern(name));
            square->style.rect.x = x;
            square->style.rect.y = y;
            square->style.rect.width = UILength::Px(size);
            square->style.rect.height = UILength::Px(size);
            square->color = color;
            square->blocksInput = true;
            canvas.Invalidate();
            return square;
        }

    }  // namespace

    class UICanvasComponentTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kCanvasScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        Scene& GetScene(const char* name) const {
            return *_manager->GetSceneByName(PoolString::Intern(name));
        }

        IntrusivePtr<UICanvasComponent> Canvas(const char* sceneName, const char* nodeName) const {
            for (const auto& node : GetScene(sceneName).GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(nodeName)) {
                    return node->GetComponent<UICanvasComponent>();
                }
            }
            return {};
        }

        void Draw(const char* sceneName) {
            _manager->SetActiveScene(PoolString::Intern(sceneName));
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    // ---- the fields -------------------------------------------------------------------------------------------------------

    TEST_F(UICanvasComponentTest, TheFieldsComeFromTheSceneConfig) {
        auto menu = Canvas("Main", "Menu");
        auto world = Canvas("Fit", "WorldCanvas");
        auto view = Canvas("Fit", "ViewCanvas");
        auto hud = Canvas("Main", "Hud");

        ASSERT_TRUE(menu && world && view && hud);
        EXPECT_TRUE(menu->_modal);
        EXPECT_FALSE(menu->_visible);
        EXPECT_EQ(menu->_scaleMode, CanvasScaleMode::ConstantPixelSize);
        EXPECT_EQ(world->_space, CanvasSpace::World);
        EXPECT_EQ(view->_space, CanvasSpace::SceneView);
        EXPECT_EQ(view->_scaleMode, CanvasScaleMode::ScaleWithScreen);  // not in the file: the default
        EXPECT_FLOAT_EQ(view->_referenceWidth, 1280.0f);
        EXPECT_TRUE(hud->_visible);
        EXPECT_TRUE(hud->_inputEnabled);
        EXPECT_FALSE(hud->_modal);
    }

    // ---- drawing ----------------------------------------------------------------------------------------------------------

    TEST_F(UICanvasComponentTest, AScreenCanvasDrawsItsElementsInOutputPixels) {
        auto hud = Canvas("Main", "Hud");
        AddSquare(*hud, "Square", 20.0f, 30.0f, 40.0f, Color{1, 2, 3, 255});

        Draw("Main");

        // The Board quad is on layer 5, the canvas on layer 10: the canvas is drawn after it
        ASSERT_EQ(_renderer->calls.size(), 2u);
        EXPECT_EQ(_renderer->calls[0].color.r, 255);
        ExpectRect(_renderer->calls[1].rect, 20.0f, 30.0f, 40.0f, 40.0f);
        EXPECT_EQ(_renderer->calls[1].color.b, 3);
    }

    TEST_F(UICanvasComponentTest, TheScaleFollowsTheOutputSize) {
        auto hud = Canvas("Main", "Hud");
        hud->_scaleMode = CanvasScaleMode::ScaleWithScreen;  // 1280 x 720 reference
        AddSquare(*hud, "Square", 100.0f, 50.0f, 40.0f);

        _renderer->width = 1280.0f;
        _renderer->height = 720.0f;
        Draw("Main");
        ExpectRect(_renderer->calls.back().rect, 100.0f, 50.0f, 40.0f, 40.0f);

        _renderer->width = 2560.0f;
        _renderer->height = 1440.0f;
        Draw("Main");
        ExpectRect(_renderer->calls.back().rect, 200.0f, 100.0f, 80.0f, 80.0f);
    }

    TEST_F(UICanvasComponentTest, TheCanvasGivesTheScenesViewTransformBack) {
        auto screen = Canvas("Fit", "ScreenCanvas");
        AddSquare(*screen, "Square", 10.0f, 10.0f, 20.0f);

        Draw("Fit");

        // The canvas is on layer 0 in pixels (scale 1); the Quad on layer 1 is drawn after it through the scene's fit (scale 2)
        ASSERT_GE(_renderer->calls.size(), 2u);
        ExpectRect(_renderer->calls.front().rect, 10.0f, 10.0f, 20.0f, 20.0f);
        ExpectRect(_renderer->calls.back().rect, 20.0f, 20.0f, 40.0f, 40.0f);
        EXPECT_FLOAT_EQ(_renderer->GetViewTransform().scale, 1.0f);  // and nothing leaks out of the pass
    }

    TEST_F(UICanvasComponentTest, ASceneViewCanvasScalesWithTheScenesView) {
        auto view = Canvas("Fit", "ViewCanvas");
        AddSquare(*view, "Square", 10.0f, 10.0f, 20.0f);

        Draw("Fit");

        const auto& calls = _renderer->calls;
        const auto it = std::find_if(calls.begin(), calls.end(), [](const DrawCall& call) { return call.color.a == 255 && call.color.r == 255 && call.color.g == 255; });
        ASSERT_NE(it, calls.end());
        ExpectRect(it->rect, 20.0f, 20.0f, 40.0f, 40.0f);  // the view is 400 x 300 in 800 x 600
    }

    TEST_F(UICanvasComponentTest, ASceneViewCanvasFillsTheViewNotTheOutput) {
        _renderer->width = 1200.0f;  // wider than the 4:3 view: bars of 200 on both sides
        _renderer->height = 600.0f;
        auto view = Canvas("Fit", "ViewCanvas");
        auto background = view->GetRoot().AddChild("Background"_intern);
        background->style.rect.anchor = UIAnchor::Stretch;
        background->color = Color{9, 9, 9, 255};
        view->Invalidate();

        Draw("Fit");

        const auto& calls = _renderer->calls;
        const auto it = std::find_if(calls.begin(), calls.end(), [](const DrawCall& call) { return call.color.r == 9; });
        ASSERT_NE(it, calls.end());
        ExpectRect(it->rect, 200.0f, 0.0f, 800.0f, 600.0f);
    }

    TEST_F(UICanvasComponentTest, AWorldCanvasFollowsItsNode) {
        auto world = Canvas("Fit", "WorldCanvas");
        AddSquare(*world, "Mark", 0.0f, 0.0f, 10.0f, Color{7, 7, 7, 255});

        Draw("Fit");
        auto find = [&] {
            const auto& calls = _renderer->calls;
            return std::find_if(calls.begin(), calls.end(), [](const DrawCall& call) { return call.color.r == 7; });
        };
        ASSERT_NE(find(), _renderer->calls.end());
        ExpectRect(find()->rect, 200.0f, 120.0f, 20.0f, 20.0f);  // (100, 60) in scene units, times the fit of 2

        auto transform = world->GetNode().GetComponent<TransformComponent>();
        transform->_x = 150.0f;  // the node moved
        Draw("Fit");
        ExpectRect(find()->rect, 300.0f, 120.0f, 20.0f, 20.0f);
    }

    TEST_F(UICanvasComponentTest, AWorldCanvasIsAsBigAsTheBoxOfItsNode) {
        auto world = Canvas("Fit", "WorldCanvas");
        auto fill = world->GetRoot().AddChild("Fill"_intern);
        fill->style.rect.anchor = UIAnchor::Stretch;
        fill->color = Color{7, 7, 7, 255};
        world->Invalidate();

        Draw("Fit");

        const auto& calls = _renderer->calls;
        const auto it = std::find_if(calls.begin(), calls.end(), [](const DrawCall& call) { return call.color.r == 7; });
        ASSERT_NE(it, calls.end());
        ExpectRect(it->rect, 200.0f, 120.0f, 160.0f, 40.0f);  // 80 x 20 units at scale 2
    }

    TEST_F(UICanvasComponentTest, AHiddenCanvasDrawsNothingAndCanBeShownByName) {
        auto menu = Canvas("Main", "Menu");
        AddSquare(*menu, "Square", 10.0f, 10.0f, 10.0f, Color{4, 4, 4, 255});

        Draw("Main");
        EXPECT_EQ(std::count_if(_renderer->calls.begin(), _renderer->calls.end(), [](const DrawCall& call) { return call.color.r == 4; }), 0);

        EXPECT_TRUE(UICanvasComponent::SetVisibleByName(GetScene("Main"), "Menu"_intern, true));
        Draw("Main");
        EXPECT_EQ(std::count_if(_renderer->calls.begin(), _renderer->calls.end(), [](const DrawCall& call) { return call.color.r == 4; }), 1);

        EXPECT_TRUE(UICanvasComponent::SetVisibleByName(GetScene("Main"), "Menu"_intern, false));
        Draw("Main");
        EXPECT_EQ(std::count_if(_renderer->calls.begin(), _renderer->calls.end(), [](const DrawCall& call) { return call.color.r == 4; }), 0);
    }

    TEST_F(UICanvasComponentTest, ShowingByANameThatNoCanvasHasChangesNothing) {
        EXPECT_FALSE(UICanvasComponent::SetVisibleByName(GetScene("Main"), "Nobody"_intern, true));
        EXPECT_FALSE(UICanvasComponent::SetVisibleByName(GetScene("Main"), "Board"_intern, true));  // a node without a canvas
    }

    TEST_F(UICanvasComponentTest, NothingIsDrawnBeforeTheOutputHasASize) {
        auto hud = Canvas("Main", "Hud");
        AddSquare(*hud, "Square", 10.0f, 10.0f, 10.0f, Color{4, 4, 4, 255});
        _renderer->width = 0.0f;
        _renderer->height = 0.0f;

        Draw("Main");

        EXPECT_EQ(std::count_if(_renderer->calls.begin(), _renderer->calls.end(), [](const DrawCall& call) { return call.color.r == 4; }), 0);
        EXPECT_FALSE(hud->GetLayout().HasLayout());
    }

    TEST_F(UICanvasComponentTest, TheLayoutIsKeptWhileNothingChangesAndFollowsTheFields) {
        auto hud = Canvas("Main", "Hud");
        AddSquare(*hud, "Square", 10.0f, 10.0f, 10.0f);

        Draw("Main");
        Draw("Main");
        Draw("Main");
        EXPECT_EQ(hud->GetLayout().GetLayoutRuns(), 1u);

        hud->_scaleMode = CanvasScaleMode::ConstantPhysicalSize;  // a field edited in the inspector
        Draw("Main");
        EXPECT_EQ(hud->GetLayout().GetLayoutRuns(), 2u);
    }

    TEST_F(UICanvasComponentTest, TheUserScaleReachesTheLayout) {
        auto hud = Canvas("Main", "Hud");
        AddSquare(*hud, "Square", 10.0f, 10.0f, 10.0f);
        CoreManager::GetUIEnvironment().SetUserScale(2.0f);

        Draw("Main");
        const Rect rect = _renderer->calls.back().rect;
        CoreManager::GetUIEnvironment().SetUserScale(1.0f);

        ExpectRect(rect, 20.0f, 20.0f, 20.0f, 20.0f);
    }

    // ---- draw and input order ---------------------------------------------------------------------------------------------

    TEST_F(UICanvasComponentTest, CanvasesAreDrawnInTheOrderOfTheirNodeLayers) {
        auto hud = Canvas("Main", "Hud");
        auto menu = Canvas("Main", "Menu");
        menu->_visible = true;
        AddSquare(*menu, "Over", 0.0f, 0.0f, 10.0f, Color{2, 2, 2, 255});
        AddSquare(*hud, "Under", 0.0f, 0.0f, 10.0f, Color{1, 1, 1, 255});

        Draw("Main");

        // Board (layer 5), Hud (10), Menu (20)
        ASSERT_EQ(_renderer->calls.size(), 3u);
        EXPECT_EQ(_renderer->calls[1].color.r, 1);
        EXPECT_EQ(_renderer->calls[2].color.r, 2);
    }

    TEST_F(UICanvasComponentTest, TheInputOrderIsTheDrawOrder) {
        auto hud = Canvas("Main", "Hud");
        auto menu = Canvas("Main", "Menu");
        Draw("Main");  // settles the order: the scene attaches its nodes while it fills its tree

        EXPECT_GT(menu->GetInputOrder(), hud->GetInputOrder());  // layer 20 over layer 10

        // The draw order of a scene: the rank counts the nodes drawn before
        Scene& main = GetScene("Main");
        const SceneNode* board = nullptr;
        for (const auto& node : main.GetRootNode().GetChildren()) {
            if (node->GetName() == "Board"_intern) {
                board = node.Get();
            }
        }
        ASSERT_NE(board, nullptr);
        EXPECT_LT(main.GetDrawRank(*board), hud->GetInputOrder());
        EXPECT_LT(hud->GetInputOrder(), menu->GetInputOrder());
    }

    TEST_F(UICanvasComponentTest, TheInputOrderFollowsALayerThatChangesAfterAttaching) {
        auto hud = Canvas("Main", "Hud");
        auto menu = Canvas("Main", "Menu");
        Draw("Main");
        const int before = hud->GetInputOrder();
        ASSERT_LT(before, menu->GetInputOrder());

        hud->GetNode().SetLayer(30);  // now above the menu
        Draw("Main");

        EXPECT_GT(hud->GetInputOrder(), menu->GetInputOrder());
    }

    TEST_F(UICanvasComponentTest, SceneDrawRankFollowsLayerThenSceneOrder) {
        Scene& fit = GetScene("Fit");
        const auto& nodes = fit.GetRootNode().GetChildren();  // ScreenCanvas, ViewCanvas, WorldCanvas on layer 0; Quad on layer 1
        ASSERT_EQ(nodes.size(), 4u);

        EXPECT_LT(fit.GetDrawRank(*nodes[0]), fit.GetDrawRank(*nodes[1]));
        EXPECT_LT(fit.GetDrawRank(*nodes[1]), fit.GetDrawRank(*nodes[2]));
        EXPECT_LT(fit.GetDrawRank(*nodes[2]), fit.GetDrawRank(*nodes[3]));  // the higher layer is last

        nodes[0]->SetLayer(5);  // moving the first node up puts it behind nothing
        EXPECT_GT(fit.GetDrawRank(*nodes[0]), fit.GetDrawRank(*nodes[3]));
        EXPECT_LT(fit.GetDrawRank(*nodes[1]), fit.GetDrawRank(*nodes[2]));
    }

    // ---- input ------------------------------------------------------------------------------------------------------------

    TEST_F(UICanvasComponentTest, AClickOnAnElementIsConsumedAndOneOutsideReachesTheGame) {
        auto hud = Canvas("Main", "Hud");
        AddSquare(*hud, "Button", 100.0f, 100.0f, 50.0f);
        Draw("Main");
        GameMouseRecorder game;
        UIInput& input = CoreManager::GetUIInput();

        EXPECT_TRUE(input.DispatchMouseButtonDown(Button(120.0f, 120.0f, true)));
        EXPECT_EQ(game.downs, 0);

        EXPECT_FALSE(input.DispatchMouseButtonDown(Button(400.0f, 400.0f, true)));
        EXPECT_EQ(game.downs, 1);

        input.DispatchMouseButtonUp(Button(120.0f, 120.0f, false));
    }

    TEST_F(UICanvasComponentTest, APressThatTheCanvasTookIsFollowedToItsRelease) {
        auto hud = Canvas("Main", "Hud");
        AddSquare(*hud, "Button", 100.0f, 100.0f, 50.0f);
        Draw("Main");
        GameMouseRecorder game;
        UIInput& input = CoreManager::GetUIInput();

        ASSERT_TRUE(input.DispatchMouseButtonDown(Button(120.0f, 120.0f, true)));
        EXPECT_TRUE(hud->HoldsPress());
        EXPECT_TRUE(input.DispatchMouseMotion(Motion(700.0f, 500.0f)));  // dragged out of the button
        EXPECT_TRUE(input.DispatchMouseButtonUp(Button(700.0f, 500.0f, false)));
        EXPECT_FALSE(hud->HoldsPress());
        EXPECT_EQ(game.ups, 0);
        EXPECT_EQ(game.motions, 0);

        EXPECT_FALSE(input.DispatchMouseMotion(Motion(700.0f, 500.0f)));  // the press is over: motion in the open belongs to the game
        EXPECT_EQ(game.motions, 1);
    }

    TEST_F(UICanvasComponentTest, APressThatBeganOutsideIsNotFollowedIntoTheCanvas) {
        auto hud = Canvas("Main", "Hud");
        AddSquare(*hud, "Button", 100.0f, 100.0f, 50.0f);
        Draw("Main");
        GameMouseRecorder game;
        UIInput& input = CoreManager::GetUIInput();

        ASSERT_FALSE(input.DispatchMouseButtonDown(Button(400.0f, 400.0f, true)));
        EXPECT_FALSE(hud->HoldsPress());
        EXPECT_FALSE(input.DispatchMouseMotion(Motion(120.0f, 120.0f, SDL_BUTTON_LMASK)));  // dragging over the button: the game keeps its drag
        EXPECT_EQ(game.motions, 1);
    }

    TEST_F(UICanvasComponentTest, HoveringOverAnElementIsConsumedSoTheGameUnderItSeesNoHover) {
        auto hud = Canvas("Main", "Hud");
        AddSquare(*hud, "Button", 100.0f, 100.0f, 50.0f);
        Draw("Main");
        GameMouseRecorder game;
        UIInput& input = CoreManager::GetUIInput();

        EXPECT_TRUE(input.DispatchMouseMotion(Motion(120.0f, 120.0f)));
        EXPECT_FALSE(input.DispatchMouseMotion(Motion(400.0f, 400.0f)));
        EXPECT_EQ(game.motions, 1);
    }

    TEST_F(UICanvasComponentTest, ADecorationThatDoesNotBlockInputLetsClicksThrough) {
        auto hud = Canvas("Main", "Hud");
        auto label = AddSquare(*hud, "Label", 100.0f, 100.0f, 50.0f);
        label->blocksInput = false;
        Draw("Main");
        GameMouseRecorder game;

        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(120.0f, 120.0f, true)));
        EXPECT_EQ(game.downs, 1);
    }

    TEST_F(UICanvasComponentTest, TheClickIsMappedThroughTheCanvasScale) {
        auto hud = Canvas("Main", "Hud");
        hud->_scaleMode = CanvasScaleMode::ScaleWithScreen;
        AddSquare(*hud, "Button", 100.0f, 100.0f, 50.0f);  // at scale 2 on 2560 x 1440: pixels 200..300
        _renderer->width = 2560.0f;
        _renderer->height = 1440.0f;
        Draw("Main");
        UIInput& input = CoreManager::GetUIInput();

        EXPECT_FALSE(input.DispatchMouseButtonDown(Button(120.0f, 120.0f, true)));  // inside the unscaled square, outside the scaled one
        EXPECT_TRUE(input.DispatchMouseButtonDown(Button(250.0f, 250.0f, true)));
        input.DispatchMouseButtonUp(Button(250.0f, 250.0f, false));
    }

    TEST_F(UICanvasComponentTest, ASceneViewCanvasTakesTheClickInSceneUnits) {
        auto view = Canvas("Fit", "ViewCanvas");
        AddSquare(*view, "Button", 100.0f, 100.0f, 50.0f);  // scene units: output pixels 200..300 at the fit of 2
        Draw("Fit");
        UIInput& input = CoreManager::GetUIInput();

        EXPECT_FALSE(input.DispatchMouseButtonDown(Button(120.0f, 120.0f, true)));
        EXPECT_TRUE(input.DispatchMouseButtonDown(Button(250.0f, 250.0f, true)));
        input.DispatchMouseButtonUp(Button(250.0f, 250.0f, false));
    }

    TEST_F(UICanvasComponentTest, ACanvasWithoutALayoutConsumesNothing) {
        auto hud = Canvas("Main", "Hud");
        AddSquare(*hud, "Button", 100.0f, 100.0f, 50.0f);
        _manager->SetActiveScene("Main"_intern);  // active, but never drawn
        GameMouseRecorder game;

        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(120.0f, 120.0f, true)));
        EXPECT_EQ(game.downs, 1);
    }

    TEST_F(UICanvasComponentTest, AHiddenOrDisabledCanvasConsumesNothing) {
        auto hud = Canvas("Main", "Hud");
        AddSquare(*hud, "Button", 100.0f, 100.0f, 50.0f);
        Draw("Main");
        UIInput& input = CoreManager::GetUIInput();

        hud->_inputEnabled = false;
        EXPECT_FALSE(input.DispatchMouseButtonDown(Button(120.0f, 120.0f, true)));

        hud->_inputEnabled = true;
        hud->_visible = false;
        EXPECT_FALSE(input.DispatchMouseButtonDown(Button(120.0f, 120.0f, true)));

        hud->_visible = true;
        EXPECT_TRUE(input.DispatchMouseButtonDown(Button(120.0f, 120.0f, true)));
        input.DispatchMouseButtonUp(Button(120.0f, 120.0f, false));
    }

    TEST_F(UICanvasComponentTest, ACanvasOfAnInactiveSceneConsumesNothing) {
        auto other = Canvas("Other", "Hud");
        AddSquare(*other, "Button", 100.0f, 100.0f, 50.0f);
        _manager->SetActiveScene("Other"_intern);
        _manager->DrawAll();  // the laid-out canvas of "Other" is current
        _manager->SetActiveScene("Main"_intern);
        GameMouseRecorder game;

        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(120.0f, 120.0f, true)));
        EXPECT_EQ(game.downs, 1);
    }

    // ---- several canvases -------------------------------------------------------------------------------------------------

    TEST_F(UICanvasComponentTest, OfTwoOverlappingCanvasesTheTopOneTakesThePress) {
        auto hud = Canvas("Main", "Hud");
        auto menu = Canvas("Main", "Menu");
        menu->_visible = true;
        menu->_modal = false;  // a plain canvas above another: the dialog case is the next test
        AddSquare(*hud, "Under", 100.0f, 100.0f, 100.0f);
        AddSquare(*menu, "Over", 150.0f, 150.0f, 100.0f);
        Draw("Main");
        UIInput& input = CoreManager::GetUIInput();

        ASSERT_TRUE(input.DispatchMouseButtonDown(Button(170.0f, 170.0f, true)));  // inside both: the menu is asked first
        EXPECT_TRUE(menu->HoldsPress());
        EXPECT_FALSE(hud->HoldsPress());
        input.DispatchMouseButtonUp(Button(170.0f, 170.0f, false));

        ASSERT_TRUE(input.DispatchMouseButtonDown(Button(120.0f, 120.0f, true)));  // only inside the lower one
        EXPECT_TRUE(hud->HoldsPress());
        EXPECT_FALSE(menu->HoldsPress());
        input.DispatchMouseButtonUp(Button(120.0f, 120.0f, false));
    }

    TEST_F(UICanvasComponentTest, AModalCanvasConsumesEveryEventWhileItIsShown) {
        auto hud = Canvas("Main", "Hud");
        auto menu = Canvas("Main", "Menu");
        AddSquare(*hud, "Under", 100.0f, 100.0f, 100.0f);
        AddSquare(*menu, "Dialog", 400.0f, 300.0f, 100.0f);
        menu->_visible = true;
        Draw("Main");
        GameMouseRecorder game;
        UIInput& input = CoreManager::GetUIInput();

        EXPECT_TRUE(input.DispatchMouseButtonDown(Button(120.0f, 120.0f, true)));  // on the button below: it never sees it
        EXPECT_FALSE(hud->HoldsPress());
        EXPECT_TRUE(menu->HoldsPress());
        input.DispatchMouseButtonUp(Button(120.0f, 120.0f, false));
        EXPECT_TRUE(input.DispatchMouseButtonDown(Button(700.0f, 50.0f, true)));  // on nothing: the game does not see it either
        input.DispatchMouseButtonUp(Button(700.0f, 50.0f, false));
        SDL_MouseWheelEvent wheel{};
        wheel.mouse_x = 700.0f;
        wheel.mouse_y = 50.0f;
        EXPECT_TRUE(input.DispatchMouseWheel(wheel));
        EXPECT_EQ(game.downs, 0);

        menu->_visible = false;  // the dialog is closed: everything is as before
        EXPECT_TRUE(input.DispatchMouseButtonDown(Button(120.0f, 120.0f, true)));
        EXPECT_TRUE(hud->HoldsPress());
        input.DispatchMouseButtonUp(Button(120.0f, 120.0f, false));
        EXPECT_FALSE(input.DispatchMouseButtonDown(Button(700.0f, 50.0f, true)));
        EXPECT_EQ(game.downs, 1);
    }

    TEST_F(UICanvasComponentTest, ADestroyedSceneLeavesNoHandlerBehind) {
        auto hud = Canvas("Main", "Hud");
        AddSquare(*hud, "Button", 100.0f, 100.0f, 50.0f);
        Draw("Main");
        hud.Reset();

        _manager.Reset();  // every canvas unregisters in its destructor

        GameMouseRecorder game;
        EXPECT_FALSE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(120.0f, 120.0f, true)));
        EXPECT_EQ(game.downs, 1);
    }

}  // namespace BECore::Tests
