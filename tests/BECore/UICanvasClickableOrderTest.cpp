#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Components/ClickableComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // A canvas on layer 10 between two scene targets: "Under" (layer 5) is drawn before it, "Over" (layer 15) after it
        constexpr const char* kScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Under" layer="5"><components>
      <component type="TransformComponent" x="0" y="0" width="300" height="300"/>
      <component type="ClickableComponent" actionName="ActUnder"/>
    </components></node>
    <node name="Canvas" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
    </components></node>
    <node name="Over" layer="15"><components>
      <component type="TransformComponent" x="400" y="0" width="100" height="100"/>
      <component type="ClickableComponent" actionName="ActOver"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        // Squares of the canvas: Shielding covers part of "Under", Covered lies below "Over", Free has nothing around it
        constexpr float kShieldingX = 100.0f;
        constexpr float kCoveredX = 410.0f;
        constexpr float kFreeX = 600.0f;
        constexpr float kSquareY = 10.0f;
        constexpr float kSquareSize = 50.0f;
        constexpr float kInsideSquare = 20.0f;
        constexpr float kUnderOnly = 250.0f;

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

        class Recorder : public SubscriptionHolder {
        public:
            Recorder() {
                Subscribe<InputEvents::ActionEvent, &Recorder::OnAction>(this);
            }

            Vector<PoolString> actions;

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                actions.push_back(event.action);
            }
        };

        /// An interactive square that counts what the canvas tells it
        class Probe : public UIElement {
        public:
            void OnPress(float, float) override {
                ++presses;
            }
            void OnClick() override {
                ++clicks;
            }

            int presses = 0;
            int clicks = 0;
        };

        /// A handler that sits above the canvas and takes the motion over a rect when `take` is set
        class Taker : public IUIInputHandler {
        public:
            [[nodiscard]] bool ContainsPoint(float x, float y) const override {
                return x >= area.x && x <= area.x + area.w && y >= area.y && y <= area.y + area.h;
            }
            bool OnMouseMotion(const SDL_MouseMotionEvent&) override {
                return take;
            }

            Rect area;
            bool take = false;
        };

    }  // namespace

    class UICanvasClickableOrderTest : public ::testing::Test {
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
            ASSERT_TRUE(deserializer.LoadFromString(kScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();

            for (const auto& node : _manager->GetActiveScene()->GetRootNode().GetChildren()) {
                if (node->GetName() == "Canvas"_intern) {
                    _canvas = node->GetComponent<UICanvasComponent>();
                }
            }
            ASSERT_TRUE(_canvas);
            _shielding = AddProbe("Shielding", kShieldingX);
            _covered = AddProbe("Covered", kCoveredX);
            _free = AddProbe("Free", kFreeX);
            _manager->DrawAll();
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        IntrusivePtr<Probe> AddProbe(const char* name, float x) {
            auto probe = New<Probe>();
            probe->name = PoolString::Intern(name);
            probe->style.rect.x = UILength::Px(x);
            probe->style.rect.y = UILength::Px(kSquareY);
            probe->style.rect.width = UILength::Px(kSquareSize);
            probe->style.rect.height = UILength::Px(kSquareSize);
            probe->interactive = true;
            probe->blocksInput = true;
            _canvas->GetRoot().children.push_back(probe);
            _canvas->Invalidate();
            return probe;
        }

        bool Press(float x, float y) {
            return CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));
        }

        bool Release(float x, float y) {
            return CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false));
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
        IntrusivePtr<Probe> _shielding;
        IntrusivePtr<Probe> _covered;
        IntrusivePtr<Probe> _free;
    };

    TEST_F(UICanvasClickableOrderTest, ACanvasDrawnAboveAClickableShieldsIt) {
        Recorder recorder;

        EXPECT_TRUE(Press(kShieldingX + kInsideSquare, kSquareY + kInsideSquare));

        EXPECT_EQ(_shielding->presses, 1);
        EXPECT_TRUE(recorder.actions.empty()) << "the clickable below the canvas element got the press";
    }

    TEST_F(UICanvasClickableOrderTest, TheClickableBelowTheCanvasTakesWhatTheCanvasLeavesFree) {
        Recorder recorder;

        EXPECT_TRUE(Press(kUnderOnly, kUnderOnly));

        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "ActUnder"_intern);
        EXPECT_EQ(_shielding->presses, 0);
    }

    TEST_F(UICanvasClickableOrderTest, ACanvasDrawnBelowAClickableGetsNoPressWhereTheClickableIs) {
        Recorder recorder;

        EXPECT_TRUE(Press(kCoveredX + kInsideSquare, kSquareY + kInsideSquare));

        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "ActOver"_intern);
        EXPECT_EQ(_covered->presses, 0);
        EXPECT_FALSE(_canvas->HoldsPress());
    }

    TEST_F(UICanvasClickableOrderTest, TheCanvasGetsThePressOutsideTheClickableAbove) {
        Recorder recorder;

        EXPECT_TRUE(Press(kFreeX + kInsideSquare, kSquareY + kInsideSquare));

        EXPECT_EQ(_free->presses, 1);
        EXPECT_TRUE(recorder.actions.empty());
    }

    TEST_F(UICanvasClickableOrderTest, TheOrderOfACanvasAndAClickableFollowsTheirLayers) {
        Scene& scene = *_manager->GetActiveScene();
        const ClickableComponent* under = nullptr;
        const ClickableComponent* over = nullptr;
        for (const auto& node : scene.GetRootNode().GetChildren()) {
            if (node->GetName() == "Under"_intern) {
                under = node->GetComponent<ClickableComponent>().Get();
            } else if (node->GetName() == "Over"_intern) {
                over = node->GetComponent<ClickableComponent>().Get();
            }
        }
        ASSERT_TRUE(under && over);

        EXPECT_LT(scene.GetInputOrder(*under), _canvas->GetInputOrder());
        EXPECT_LT(_canvas->GetInputOrder(), scene.GetInputOrder(*over));
    }

    TEST_F(UICanvasClickableOrderTest, AClickableAboveTheCanvasTakesTheMotionAndTheCanvasDoesNotLightUp) {
        const float x = kCoveredX + kInsideSquare;
        const float y = kSquareY + kInsideSquare;

        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseMotion(Motion(x, y)));

        EXPECT_FALSE(_covered->hovered) << "the element under the clickable shows no hover";
    }

    TEST_F(UICanvasClickableOrderTest, AClickableAboveTheCanvasTakingThePointerClearsTheHover) {
        UIInput& input = CoreManager::GetUIInput();
        input.DispatchMouseMotion(Motion(kFreeX + kInsideSquare, kSquareY + kInsideSquare));
        ASSERT_TRUE(_free->hovered);

        input.DispatchMouseMotion(Motion(kCoveredX + kInsideSquare, kSquareY + kInsideSquare));

        EXPECT_FALSE(_free->hovered) << "the pointer left the canvas for the clickable drawn above it";
    }

    TEST_F(UICanvasClickableOrderTest, ACanvasDrawnAboveAClickableTakesTheMotionOverItsElement) {
        UIInput& input = CoreManager::GetUIInput();

        EXPECT_TRUE(input.DispatchMouseMotion(Motion(kShieldingX + kInsideSquare, kSquareY + kInsideSquare)));
        EXPECT_TRUE(_shielding->hovered);

        EXPECT_TRUE(input.DispatchMouseMotion(Motion(kUnderOnly, kUnderOnly))) << "the clickable takes it where the canvas has nothing";
        EXPECT_FALSE(_shielding->hovered);
    }

    TEST_F(UICanvasClickableOrderTest, ADragOfTheGamePassesOverAClickableAboveTheCanvas) {
        const bool consumed = CoreManager::GetUIInput().DispatchMouseMotion(Motion(kCoveredX + kInsideSquare, kSquareY + kInsideSquare, SDL_BUTTON_LMASK));

        EXPECT_FALSE(consumed);
        EXPECT_FALSE(_covered->hovered);
    }

    TEST_F(UICanvasClickableOrderTest, ALeaveKeepsAHeldPress) {
        const float x = kFreeX + kInsideSquare;
        const float y = kSquareY + kInsideSquare;
        CoreManager::GetUIInput().DispatchMouseMotion(Motion(x, y));
        ASSERT_TRUE(Press(x, y));
        ASSERT_TRUE(_canvas->HoldsPress());
        ASSERT_TRUE(_free->hovered);

        _canvas->OnPointerLeave(0);

        EXPECT_FALSE(_free->hovered);
        EXPECT_TRUE(_canvas->HoldsPress());
        EXPECT_TRUE(_free->pressed);
        EXPECT_TRUE(Release(x, y));
        EXPECT_EQ(_free->clicks, 1);
    }

    TEST_F(UICanvasClickableOrderTest, ACaptureHoldsTheCanvasWhileAHandlerAboveWantsThePointer) {
        Taker taker;
        taker.area = Rect{kFreeX - 20.0f, 0.0f, 200.0f, 100.0f};
        taker.take = true;
        UIInput& input = CoreManager::GetUIInput();
        const float x = kFreeX + kInsideSquare;
        const float y = kSquareY + kInsideSquare;
        ASSERT_TRUE(Press(x, y));
        input.Register(taker, _canvas->GetInputOrder() + 1);

        input.DispatchMouseMotion(Motion(x, y, SDL_BUTTON_LMASK));

        EXPECT_TRUE(_canvas->HoldsPress()) << "the holder got the motion first, so the press is intact";
        EXPECT_TRUE(Release(x, y));
        EXPECT_EQ(_free->clicks, 1);
        input.Unregister(taker);
    }

}  // namespace BECore::Tests
