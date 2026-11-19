#include "PointerTargetComponentTest.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/PointerId.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/Require.h>
#include <CoreSDL/SDLEvents.h>
#include <Generated/PointerTargetComponentTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    void PointerTargetTestComponent::Note(TargetScript* script, TargetCall call, const PointerEvent& event) {
        if (script == nullptr) {
            return;
        }
        script->log.push_back(TargetRecord{call, event});
        if (script->hook) {
            script->hook(call);
        }
    }

    bool PointerTargetTestComponent::ContainsScenePoint(float sceneX, float sceneY) const {
        return scenePredicate != nullptr ? scenePredicate(sceneX, sceneY) : PointerTargetComponent::ContainsScenePoint(sceneX, sceneY);
    }

    bool PointerTargetTestComponent::OnPointerPress(const PointerEvent& event) {
        TargetScript* const target = script;
        Note(target, TargetCall::Press, event);
        return target == nullptr || target->takePress;
    }

    void PointerTargetTestComponent::OnPointerRelease(const PointerEvent& event) {
        Note(script, TargetCall::Release, event);
    }

    void PointerTargetTestComponent::OnPointerEnter(const PointerEvent& event) {
        Note(script, TargetCall::Enter, event);
    }

    void PointerTargetTestComponent::OnPointerExit(const PointerEvent& event) {
        Note(script, TargetCall::Exit, event);
    }

    bool PointerTargetTestComponent::OnPointerMove(const PointerEvent& event) {
        TargetScript* const target = script;
        Note(target, TargetCall::Move, event);
        return target == nullptr || target->takeMove;
    }

    void PointerTargetTestComponent::OnPointerDrag(const PointerEvent& event) {
        Note(script, TargetCall::Drag, event);
    }

    bool PointerTargetTestComponent::OnPointerWheel(const PointerEvent& event) {
        TargetScript* const target = script;
        Note(target, TargetCall::Wheel, event);
        return target != nullptr && target->takeWheel;
    }

    namespace {

        constexpr float kOutputWidth = 800.0f;
        constexpr float kOutputHeight = 600.0f;
        constexpr float kBoxSize = 100.0f;
        constexpr float kInside = 10.0f;
        constexpr float kOutside = 500.0f;
        constexpr float kFitScale = 2.0f;      // the "Fit" scene: a 400 x 200 view in the 800 x 600 output
        constexpr float kFitOffsetY = 100.0f;  // the bar above the fitted view
        constexpr uint32_t kMouse = 1;
        constexpr uint32_t kSecondPointer = 2;

        String TargetNode(const char* name, float x, float y, float width, float height, int layer = 0) {
            return Format(R"(<node name="{}" layer="{}"><components>
  <component type="TransformComponent" x="{}" y="{}" width="{}" height="{}"/>
  <component type="PointerTargetTestComponent"/>
</components></node>)",
                          name, layer, x, y, width, height);
        }

        String SingleSceneXml(const String& nodes, const char* sceneAttributes = "") {
            return Format(R"(<SceneManager><scenes><scene type="Scene" name="S" {}><nodes>{}</nodes></scene></scenes></SceneManager>)", sceneAttributes, nodes);
        }

        class GameRecorder : public SubscriptionHolder {
        public:
            GameRecorder() {
                Subscribe<SDLEvents::MouseButtonDownEvent, &GameRecorder::OnDown>(this);
                Subscribe<SDLEvents::MouseButtonUpEvent, &GameRecorder::OnUp>(this);
                Subscribe<SDLEvents::MouseMotionEvent, &GameRecorder::OnMotion>(this);
                Subscribe<SDLEvents::MouseWheelEvent, &GameRecorder::OnWheel>(this);
            }

            int downs = 0;
            int ups = 0;
            int motions = 0;
            int wheels = 0;

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
            void OnWheel(const SDLEvents::MouseWheelEvent&) {
                ++wheels;
            }
        };

        bool Press(float x, float y, uint8_t button = SDL_BUTTON_LEFT, uint32_t which = kMouse, uint8_t clicks = 1) {
            SDL_MouseButtonEvent event{};
            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.which = which;
            event.button = button;
            event.down = true;
            event.clicks = clicks;
            event.x = x;
            event.y = y;
            return CoreManager::GetUIInput().DispatchMouseButtonDown(event);
        }

        bool Release(float x, float y, uint8_t button = SDL_BUTTON_LEFT, uint32_t which = kMouse) {
            SDL_MouseButtonEvent event{};
            event.type = SDL_EVENT_MOUSE_BUTTON_UP;
            event.which = which;
            event.button = button;
            event.x = x;
            event.y = y;
            return CoreManager::GetUIInput().DispatchMouseButtonUp(event);
        }

        bool Move(float x, float y, uint32_t which = kMouse, SDL_MouseButtonFlags held = 0, float dx = 0.0f, float dy = 0.0f) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.which = which;
            event.state = held;
            event.x = x;
            event.y = y;
            event.xrel = dx;
            event.yrel = dy;
            return CoreManager::GetUIInput().DispatchMouseMotion(event);
        }

        bool Wheel(float x, float y, float amountX, float amountY, uint32_t which = kMouse) {
            SDL_MouseWheelEvent event{};
            event.type = SDL_EVENT_MOUSE_WHEEL;
            event.which = which;
            event.x = amountX;
            event.y = amountY;
            event.mouse_x = x;
            event.mouse_y = y;
            return CoreManager::GetUIInput().DispatchMouseWheel(event);
        }

        Vector<TargetCall> Calls(const TargetScript& script) {
            Vector<TargetCall> calls;
            for (const TargetRecord& record : script.log) {
                calls.push_back(record.call);
            }
            return calls;
        }

        /// A square of the canvas that takes the pointer and counts the presses
        class CanvasProbe : public UIElement {
        public:
            void OnPress(float, float) override {
                ++presses;
            }

            int presses = 0;
        };

        constexpr float kCircleMiddle = 50.0f;
        bool IsInsideTheUnitDiamond(float sceneX, float sceneY) {
            return std::abs(sceneX - kCircleMiddle) + std::abs(sceneY - kCircleMiddle) <= kCircleMiddle;
        }

    }  // namespace

    class PointerTargetComponentTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            _renderer->width = kOutputWidth;
            _renderer->height = kOutputHeight;
            CoreManager::GetRendererManager().SetRenderer(_renderer);
        }

        void TearDown() override {
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        void LoadScenes(const String& xml) {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(xml.c_str())) << xml.c_str();
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void LoadNodes(const String& nodes, const char* sceneAttributes = "") {
            ASSERT_NO_FATAL_FAILURE(LoadScenes(SingleSceneXml(nodes, sceneAttributes)));
        }

        void Draw() {
            _manager->DrawAll();
        }

        SceneNode& Node(const char* name) {
            for (const auto& node : _manager->GetActiveScene()->GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(name)) {
                    return *node;
                }
            }
            ADD_FAILURE() << "no node " << name;
            return _manager->GetActiveScene()->GetRootNode();
        }

        PointerTargetTestComponent& Bind(const char* name, TargetScript& script) {
            const auto target = Node(name).GetComponent<PointerTargetTestComponent>();
            EXPECT_TRUE(target) << name;
            PointerTargetTestComponent& component = Require(target);
            component.script = &script;
            return component;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        TargetScript _a;
        TargetScript _b;
    };

    TEST_F(PointerTargetComponentTest, TheBaseIsReflectedAndTheTestTargetDerivesFromIt) {
        const TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* base = registry.Find("PointerTargetComponent");
        const TypeInfo* derived = registry.Find("PointerTargetTestComponent");
        ASSERT_NE(base, nullptr);
        ASSERT_NE(derived, nullptr);

        EXPECT_TRUE(registry.IsDerivedFrom(*derived, *base));
        EXPECT_TRUE(registry.IsDerivedFrom(*base, Require(registry.Find("IComponent"))));
    }

    TEST_F(PointerTargetComponentTest, APressCarriesTheSceneAndOutputPositionTheButtonTheClicksAndThePointer) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        GameRecorder game;

        EXPECT_TRUE(Press(kInside, kInside + 5.0f, SDL_BUTTON_RIGHT, kSecondPointer, 2));

        ASSERT_EQ(_a.log.size(), 1u);
        const PointerEvent& event = _a.log[0].event;
        EXPECT_EQ(_a.log[0].call, TargetCall::Press);
        EXPECT_FLOAT_EQ(event.sceneX, kInside);
        EXPECT_FLOAT_EQ(event.sceneY, kInside + 5.0f);
        EXPECT_FLOAT_EQ(event.outputX, kInside);
        EXPECT_FLOAT_EQ(event.outputY, kInside + 5.0f);
        EXPECT_EQ(event.which, kSecondPointer);
        EXPECT_EQ(event.button, SDL_BUTTON_RIGHT);
        EXPECT_EQ(event.clicks, 2);
        EXPECT_FALSE(event.touch);
        EXPECT_EQ(game.downs, 0);
    }

    TEST_F(PointerTargetComponentTest, ATouchPointerIsMarkedTouch) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);

        Press(kInside, kInside, SDL_BUTTON_LEFT, SDL_TOUCH_MOUSEID);
        Press(kInside, kInside, SDL_BUTTON_LEFT, kFingerPointerBase + 1);

        ASSERT_EQ(_a.log.size(), 2u);
        EXPECT_TRUE(_a.log[0].event.touch);
        EXPECT_TRUE(_a.log[1].event.touch);
    }

    TEST_F(PointerTargetComponentTest, APressOutsideTheShapeIsRawAndATargetThatDeclinesIsSkipped) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        GameRecorder game;

        EXPECT_FALSE(Press(kOutside, kOutside));
        EXPECT_EQ(game.downs, 1);
        EXPECT_TRUE(_a.log.empty());

        _a.takePress = false;
        EXPECT_FALSE(Press(kInside, kInside));
        EXPECT_EQ(game.downs, 2);
        EXPECT_EQ(_a.Count(TargetCall::Press), 1u);

        EXPECT_FALSE(Release(kInside, kInside));
        EXPECT_EQ(game.ups, 1);
        EXPECT_EQ(_a.Count(TargetCall::Release), 0u) << "a press nobody took holds nothing";
    }

    TEST_F(PointerTargetComponentTest, TheReleaseReachesTheTargetWhereverThePointerIsAndIsNotRaw) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        GameRecorder game;
        ASSERT_TRUE(Press(kInside, kInside));

        EXPECT_TRUE(Release(kOutside, kOutside));

        ASSERT_EQ(_a.Count(TargetCall::Release), 1u);
        EXPECT_FLOAT_EQ(_a.log.back().event.sceneX, kOutside);
        EXPECT_EQ(_a.log.back().event.button, SDL_BUTTON_LEFT);
        EXPECT_EQ(game.ups, 0);
    }

    TEST_F(PointerTargetComponentTest, AReleaseOfAPressOfTheGameStaysRawOverTheShape) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 200, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        GameRecorder game;
        ASSERT_FALSE(Press(kInside, kInside));

        EXPECT_FALSE(Release(200.0f + kInside, kInside));

        EXPECT_EQ(game.ups, 1);
        EXPECT_EQ(_a.Count(TargetCall::Release), 0u);
    }

    TEST_F(PointerTargetComponentTest, ARightPressOfTheGameThenALeftPressOnTheTargetThenTheRightReleaseOverItGoesRawOnce) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 200, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        GameRecorder game;
        ASSERT_FALSE(Press(kInside, kInside, SDL_BUTTON_RIGHT));
        ASSERT_TRUE(Press(200.0f + kInside, kInside, SDL_BUTTON_LEFT));

        EXPECT_FALSE(Release(200.0f + kInside, kInside, SDL_BUTTON_RIGHT));
        EXPECT_EQ(game.ups, 1);
        EXPECT_EQ(_a.Count(TargetCall::Release), 0u);

        EXPECT_TRUE(Release(200.0f + kInside, kInside, SDL_BUTTON_LEFT));
        EXPECT_EQ(game.ups, 1);
        EXPECT_EQ(_a.Count(TargetCall::Release), 1u);
    }

    TEST_F(PointerTargetComponentTest, AReleaseOfAnotherPointerOfTheGameStaysRawWhileTheTargetHoldsItsOwn) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 200, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        GameRecorder game;
        ASSERT_FALSE(Press(kInside, kInside, SDL_BUTTON_LEFT, kSecondPointer));
        ASSERT_TRUE(Press(200.0f + kInside, kInside));

        EXPECT_FALSE(Release(200.0f + kInside, kInside, SDL_BUTTON_LEFT, kSecondPointer));
        EXPECT_EQ(game.ups, 1);

        EXPECT_TRUE(Release(200.0f + kInside, kInside));
        EXPECT_EQ(game.ups, 1);
        EXPECT_EQ(_a.Count(TargetCall::Release), 1u);
    }

    TEST_F(PointerTargetComponentTest, TheMotionOverTheShapeEntersOnceThenMovesAndLeavingExitsOnce) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        GameRecorder game;

        EXPECT_TRUE(Move(kInside, kInside));
        EXPECT_TRUE(Move(kInside + 5.0f, kInside));
        EXPECT_EQ(Calls(_a), Vector<TargetCall>({TargetCall::Enter, TargetCall::Move, TargetCall::Move}));

        EXPECT_FALSE(Move(kOutside, kInside));
        EXPECT_FALSE(Move(kOutside + 5.0f, kInside));

        EXPECT_EQ(Calls(_a), Vector<TargetCall>({TargetCall::Enter, TargetCall::Move, TargetCall::Move, TargetCall::Exit}));
        EXPECT_FLOAT_EQ(_a.log.back().event.outputX, kInside + 5.0f) << "the exit tells where the pointer was last over the target";
        EXPECT_EQ(game.motions, 2);
    }

    TEST_F(PointerTargetComponentTest, TheMotionIsTakenByDefaultSoWhatIsBelowGetsNoHover) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        GameRecorder game;

        EXPECT_TRUE(Move(kInside, kInside));

        EXPECT_EQ(game.motions, 0);
    }

    TEST_F(PointerTargetComponentTest, AMotionTheTargetDeclinesGoesToTheGameAndTheEnterIsTakenBackWithAnExit) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        _a.takeMove = false;
        GameRecorder game;

        EXPECT_FALSE(Move(kInside, kInside));

        EXPECT_EQ(Calls(_a), Vector<TargetCall>({TargetCall::Enter, TargetCall::Move, TargetCall::Exit}));
        EXPECT_EQ(game.motions, 1);
    }

    TEST_F(PointerTargetComponentTest, AMotionWhileAButtonOfTheGameIsHeldIsNotTaken) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        GameRecorder game;

        EXPECT_FALSE(Move(kInside, kInside, kMouse, SDL_BUTTON_LMASK));

        EXPECT_TRUE(_a.log.empty());
        EXPECT_EQ(game.motions, 1);
    }

    TEST_F(PointerTargetComponentTest, ADragLeavesTheShapeAndStillGetsTheStepsInSceneUnitsAndTheRelease) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize), R"(viewWidth="400" viewHeight="200")"));
        Draw();
        Bind("A", _a);
        GameRecorder game;
        // Output (100, 200) is scene (50, 50) in the fitted view
        ASSERT_TRUE(Press(100.0f, kFitOffsetY + 100.0f));

        EXPECT_TRUE(Move(700.0f, kFitOffsetY + 130.0f, kMouse, SDL_BUTTON_LMASK, 40.0f, 30.0f));
        EXPECT_TRUE(Release(700.0f, kFitOffsetY + 130.0f));

        EXPECT_EQ(Calls(_a), Vector<TargetCall>({TargetCall::Press, TargetCall::Drag, TargetCall::Release}));
        const PointerEvent& drag = _a.log[1].event;
        EXPECT_FLOAT_EQ(drag.sceneX, 350.0f);
        EXPECT_FLOAT_EQ(drag.sceneY, 65.0f);
        EXPECT_FLOAT_EQ(drag.sceneDx, 40.0f / kFitScale);
        EXPECT_FLOAT_EQ(drag.sceneDy, 30.0f / kFitScale);
        EXPECT_FLOAT_EQ(drag.outputX, 700.0f);
        EXPECT_EQ(game.motions, 0);
        EXPECT_EQ(game.ups, 0);
    }

    TEST_F(PointerTargetComponentTest, TheScenePositionOfAHoverFollowsTheLetterboxedView) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize), R"(viewWidth="400" viewHeight="200")"));
        Draw();
        Bind("A", _a);

        EXPECT_FALSE(Move(kInside, kInside)) << "output (10, 10) is above the fitted view";
        EXPECT_TRUE(Move(100.0f, kFitOffsetY + 100.0f));

        ASSERT_EQ(_a.Count(TargetCall::Move), 1u);
        EXPECT_FLOAT_EQ(_a.log.back().event.sceneX, 50.0f);
        EXPECT_FLOAT_EQ(_a.log.back().event.sceneY, 50.0f);
    }

    TEST_F(PointerTargetComponentTest, ADragStartedBeforeAnyHoverEntersNothingOutsideTheShape) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        ASSERT_TRUE(Press(kInside, kInside));

        EXPECT_TRUE(Move(kOutside, kOutside, kMouse, SDL_BUTTON_LMASK));
        Release(kOutside, kOutside);
        EXPECT_FALSE(Move(kOutside + 5.0f, kOutside));

        EXPECT_EQ(Calls(_a), Vector<TargetCall>({TargetCall::Press, TargetCall::Drag, TargetCall::Release}));
    }

    TEST_F(PointerTargetComponentTest, ADragThatWasOverTheTargetExitsOnceAfterTheReleaseWhenThePointerGoesElsewhere) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        ASSERT_TRUE(Move(kInside, kInside));
        ASSERT_TRUE(Press(kInside, kInside));
        ASSERT_TRUE(Move(kOutside, kOutside, kMouse, SDL_BUTTON_LMASK));
        ASSERT_TRUE(Release(kOutside, kOutside));
        EXPECT_EQ(_a.Count(TargetCall::Exit), 0u) << "UIInput keeps the hover of the holder";

        Move(kOutside + 5.0f, kOutside);
        Move(kOutside + 9.0f, kOutside);

        EXPECT_EQ(_a.Count(TargetCall::Exit), 1u);
        EXPECT_EQ(_a.Count(TargetCall::Enter), 1u);
    }

    TEST_F(PointerTargetComponentTest, ATargetDrawnAboveTakesThePointerAndTheOneBelowExitsOnce) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Format("{}{}", TargetNode("Low", 0, 0, kBoxSize, kBoxSize), TargetNode("High", kBoxSize / 2, 0, kBoxSize, kBoxSize))));
        Draw();
        Bind("Low", _a);
        Bind("High", _b);

        ASSERT_TRUE(Move(kInside, kInside));
        ASSERT_TRUE(Move(kBoxSize * 0.75f, kInside));
        ASSERT_TRUE(Move(kBoxSize * 0.75f + 1.0f, kInside));

        EXPECT_EQ(Calls(_a), Vector<TargetCall>({TargetCall::Enter, TargetCall::Move, TargetCall::Exit}));
        EXPECT_EQ(Calls(_b), Vector<TargetCall>({TargetCall::Enter, TargetCall::Move, TargetCall::Move}));

        ASSERT_TRUE(Move(kInside, kInside));

        EXPECT_EQ(_b.Count(TargetCall::Exit), 1u);
        EXPECT_EQ(_a.Count(TargetCall::Enter), 2u);
    }

    TEST_F(PointerTargetComponentTest, TwoPointersKeepTheirOwnPressHoverAndRelease) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Format("{}{}", TargetNode("A", 0, 0, kBoxSize, kBoxSize), TargetNode("B", 200, 0, kBoxSize, kBoxSize))));
        Draw();
        Bind("A", _a);
        Bind("B", _b);

        ASSERT_TRUE(Move(kInside, kInside, kMouse));
        ASSERT_TRUE(Move(200.0f + kInside, kInside, kSecondPointer));
        ASSERT_TRUE(Press(kInside, kInside, SDL_BUTTON_LEFT, kMouse));
        ASSERT_TRUE(Press(200.0f + kInside, kInside, SDL_BUTTON_LEFT, kSecondPointer));
        EXPECT_TRUE(Move(kOutside, kOutside, kSecondPointer, SDL_BUTTON_LMASK));
        EXPECT_TRUE(Release(kOutside, kOutside, SDL_BUTTON_LEFT, kMouse));

        EXPECT_EQ(Calls(_a), Vector<TargetCall>({TargetCall::Enter, TargetCall::Move, TargetCall::Press, TargetCall::Release}));
        EXPECT_EQ(_a.log.back().event.which, kMouse);
        EXPECT_EQ(Calls(_b), Vector<TargetCall>({TargetCall::Enter, TargetCall::Move, TargetCall::Press, TargetCall::Drag}));

        EXPECT_TRUE(Release(kOutside, kOutside, SDL_BUTTON_LEFT, kSecondPointer));
        EXPECT_EQ(_b.Count(TargetCall::Release), 1u);
        EXPECT_EQ(_b.log.back().event.which, kSecondPointer);
    }

    TEST_F(PointerTargetComponentTest, TwoPointersOverOneTargetEnterAndExitEachOnce) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);

        Move(kInside, kInside, kMouse);
        Move(kInside + 1.0f, kInside, kSecondPointer);
        Move(kOutside, kOutside, kMouse);

        EXPECT_EQ(_a.Count(TargetCall::Enter), 2u);
        ASSERT_EQ(_a.Count(TargetCall::Exit), 1u);
        EXPECT_EQ(_a.log.back().event.which, kMouse);

        Move(kOutside, kOutside, kSecondPointer);
        ASSERT_EQ(_a.Count(TargetCall::Exit), 2u);
        EXPECT_EQ(_a.log.back().event.which, kSecondPointer);
    }

    TEST_F(PointerTargetComponentTest, TheWheelIsTheTargetsOnlyWhenItAnswersTrue) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        GameRecorder game;

        EXPECT_FALSE(Wheel(kInside, kInside, 0.0f, 1.0f));
        EXPECT_EQ(game.wheels, 1);

        _a.takeWheel = true;
        EXPECT_TRUE(Wheel(kInside, kInside + 2.0f, 0.5f, -3.0f));

        EXPECT_EQ(game.wheels, 1);
        const PointerEvent& event = _a.log.back().event;
        EXPECT_FLOAT_EQ(event.wheelX, 0.5f);
        EXPECT_FLOAT_EQ(event.wheelY, -3.0f);
        EXPECT_FLOAT_EQ(event.sceneY, kInside + 2.0f);
        EXPECT_FLOAT_EQ(event.outputX, kInside);
    }

    TEST_F(PointerTargetComponentTest, TheShapeIsTheTransformRectByDefaultAndAnExplicitRectReplacesIt) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        PointerTargetTestComponent& target = Bind("A", _a);

        EXPECT_TRUE(Press(kInside, kInside));
        Release(kInside, kInside);

        target.SetShape(PointerShape::InRect(Rect{300.0f, 300.0f, 50.0f, 50.0f}));
        EXPECT_FALSE(Press(kInside, kInside));
        EXPECT_TRUE(Press(320.0f, 320.0f));
        EXPECT_EQ(_a.Count(TargetCall::Press), 2u);
    }

    TEST_F(PointerTargetComponentTest, ACircleAndAnEllipseLeaveTheCornersToWhatIsBelow) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        PointerTargetTestComponent& target = Bind("A", _a);

        PointerShape circle;
        circle.kind = PointerShapeKind::Circle;
        target.SetShape(circle);
        EXPECT_TRUE(Press(kCircleMiddle, kCircleMiddle));
        Release(kCircleMiddle, kCircleMiddle);
        EXPECT_FALSE(Press(kInside / 2, kInside / 2)) << "a corner of the rect";
        EXPECT_TRUE(Press(kCircleMiddle, 1.0f));
        Release(kCircleMiddle, 1.0f);

        PointerShape ellipse;
        ellipse.kind = PointerShapeKind::Ellipse;
        target.SetShape(ellipse);
        EXPECT_FALSE(Press(kInside / 2, kInside / 2));
        EXPECT_TRUE(Press(kCircleMiddle, kCircleMiddle));
    }

    TEST_F(PointerTargetComponentTest, AShapeOfTheGamesOwnCanBeGivenByOverridingContainsScenePoint) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        PointerTargetTestComponent& target = Bind("A", _a);
        target.scenePredicate = &IsInsideTheUnitDiamond;

        EXPECT_FALSE(Press(5.0f, 5.0f)) << "inside the rect, outside the diamond";
        EXPECT_TRUE(Press(kCircleMiddle, 10.0f));
    }

    TEST_F(PointerTargetComponentTest, ANodeWithoutATransformHasAShapeOnlyWhenTheGameSetItsOwnRect) {
        const String bare = R"(<node name="Bare"><components>
  <component type="PointerTargetTestComponent"/>
</components></node>)";
        ASSERT_NO_FATAL_FAILURE(LoadNodes(bare));
        Draw();
        PointerTargetTestComponent& target = Bind("Bare", _a);

        EXPECT_FALSE(Press(kInside, kInside));

        target.SetShape(PointerShape::InRect(Rect{0.0f, 0.0f, 50.0f, 50.0f}));
        EXPECT_TRUE(Press(kInside, kInside));

        Release(kInside, kInside);
        PointerShape transformKind;
        transformKind.rect = Rect{0.0f, 0.0f, 50.0f, 50.0f};
        target.SetShape(transformKind);
        EXPECT_FALSE(Press(kInside, kInside)) << "the transform rect kind ignores the rect it carries";
    }

    TEST_F(PointerTargetComponentTest, ABareBaseTakesTheMotionOverItsShapeAndNoPress) {
        const String bare = R"(<node name="Bare"><components>
  <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
  <component type="PointerTargetComponent"/>
</components></node>)";
        ASSERT_NO_FATAL_FAILURE(LoadNodes(bare));
        Draw();
        GameRecorder game;

        EXPECT_FALSE(Press(kInside, kInside));
        EXPECT_TRUE(Move(kInside, kInside));

        EXPECT_EQ(game.downs, 1);
        EXPECT_EQ(game.motions, 0);
    }

    TEST_F(PointerTargetComponentTest, AHiddenNodeAndAnInactiveSceneTakeNothing) {
        const String xml = Format(R"(<SceneManager><scenes>
<scene type="Scene" name="Empty"><nodes></nodes></scene>
<scene type="Scene" name="Other"><nodes>{}</nodes></scene>
</scenes></SceneManager>)",
                                  TargetNode("A", 0, 0, kBoxSize, kBoxSize));
        ASSERT_NO_FATAL_FAILURE(LoadScenes(xml));
        EXPECT_FALSE(Press(kInside, kInside));

        _manager->SetActiveScene("Other"_intern);
        Draw();
        Bind("A", _a);
        ASSERT_TRUE(Press(kInside, kInside));
        Release(kInside, kInside);

        Node("A").SetEditorFlags(EditorFlags::Hidden);
        Draw();
        EXPECT_FALSE(Press(kInside, kInside));
        EXPECT_FALSE(Move(kInside, kInside));

        Node("A").SetEditorFlags(EditorFlags::None);
        Draw();
        EXPECT_TRUE(Press(kInside, kInside));
        EXPECT_EQ(_a.Count(TargetCall::Press), 2u);
    }

    TEST_F(PointerTargetComponentTest, ADestroyedTargetIsNotCalled) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        _manager.Reset();

        EXPECT_FALSE(Press(kInside, kInside));
        EXPECT_TRUE(_a.log.empty());
    }

    TEST_F(PointerTargetComponentTest, ATargetDrawnAboveACanvasShieldsItAndOneBelowIsShieldedByIt) {
        const String xml = Format(R"(<SceneManager><scenes><scene type="Scene" name="S"><nodes>
{}
<node name="Canvas" layer="10"><components>
  <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
</components></node>
{}
</nodes></scene></scenes></SceneManager>)",
                                  TargetNode("Under", 0, 0, 300, 300, 5), TargetNode("Over", 400, 0, kBoxSize, kBoxSize, 15));
        ASSERT_NO_FATAL_FAILURE(LoadScenes(xml));
        IntrusivePtr<UICanvasComponent> canvas = Node("Canvas").GetComponent<UICanvasComponent>();
        ASSERT_TRUE(canvas);
        const auto addProbe = [&](float x) {
            auto probe = New<CanvasProbe>();
            probe->style.rect.x = UILength::Px(x);
            probe->style.rect.y = UILength::Px(10.0f);
            probe->style.rect.width = UILength::Px(50.0f);
            probe->style.rect.height = UILength::Px(50.0f);
            probe->interactive = true;
            probe->blocksInput = true;
            canvas->GetRoot().children.push_back(probe);
            return probe;
        };
        const IntrusivePtr<CanvasProbe> shielding = addProbe(100.0f);
        const IntrusivePtr<CanvasProbe> covered = addProbe(410.0f);
        canvas->Invalidate();
        Draw();
        Bind("Under", _a);
        Bind("Over", _b);

        EXPECT_TRUE(Press(120.0f, 20.0f));
        EXPECT_EQ(shielding->presses, 1);
        EXPECT_TRUE(_a.log.empty()) << "the canvas drawn above takes the press over the target below it";

        Release(120.0f, 20.0f);
        EXPECT_TRUE(Press(420.0f, 20.0f));
        EXPECT_EQ(covered->presses, 0);
        EXPECT_EQ(_b.Count(TargetCall::Press), 1u) << "the target drawn above the canvas takes it first";

        Release(420.0f, 20.0f);
        EXPECT_TRUE(Press(250.0f, 250.0f));
        EXPECT_EQ(_a.Count(TargetCall::Press), 1u) << "outside the canvas element the target below gets the press";
    }

    TEST_F(PointerTargetComponentTest, TheAreaOutsideAClipPushedBeforeTheTargetIsNoTargetAndTheLowerOneTakesIt) {
        const String clipped = Format(R"(<node name="Clipped"><components>
  <component type="ClipPushTestComponent" x="0" y="0" width="{}" height="{}"/>
  <component type="TransformComponent" x="0" y="0" width="{}" height="{}"/>
  <component type="PointerTargetTestComponent"/>
  <component type="ClipPopTestComponent"/>
</components></node>)",
                                      kBoxSize / 2, kBoxSize, kBoxSize, kBoxSize);
        ASSERT_NO_FATAL_FAILURE(LoadNodes(Format("{}{}", TargetNode("Under", 0, 0, kBoxSize, kBoxSize), clipped)));
        Draw();
        Bind("Under", _a);
        Bind("Clipped", _b);

        EXPECT_TRUE(Press(kInside, kInside));
        Release(kInside, kInside);
        EXPECT_TRUE(Press(kBoxSize * 0.75f, kInside));

        EXPECT_EQ(_b.Count(TargetCall::Press), 1u);
        EXPECT_EQ(_a.Count(TargetCall::Press), 1u);
    }

    // A callback may destroy the component: the scene is reset (or the node removed) inside it, and the dispatch must go on without
    // touching the component. The debug asserts of the build run in these tests.

    TEST_F(PointerTargetComponentTest, ASceneResetInsideThePressCallbackDoesNotCrash) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        _a.hook = [&](TargetCall) { _manager.Reset(); };

        EXPECT_TRUE(Press(kInside, kInside));

        EXPECT_EQ(_a.Count(TargetCall::Press), 1u);
        EXPECT_FALSE(Release(kInside, kInside)) << "the target is gone with its press";
    }

    TEST_F(PointerTargetComponentTest, ASceneResetInsideTheReleaseCallbackDoesNotCrash) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        ASSERT_TRUE(Press(kInside, kInside));
        _a.hook = [&](TargetCall) { _manager.Reset(); };

        EXPECT_TRUE(Release(kInside, kInside));

        EXPECT_EQ(_a.Count(TargetCall::Release), 1u);
    }

    TEST_F(PointerTargetComponentTest, ASceneResetInsideTheEnterCallbackStopsTheMotionThere) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        _a.hook = [&](TargetCall call) {
            if (call == TargetCall::Enter) {
                _manager.Reset();
            }
        };

        Move(kInside, kInside);

        EXPECT_EQ(Calls(_a), Vector<TargetCall>({TargetCall::Enter}));
        EXPECT_FALSE(Move(kInside, kInside));
    }

    TEST_F(PointerTargetComponentTest, TheComponentRemovedFromItsNodeInsideTheMoveCallbackDoesNotCrash) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        _a.hook = [&](TargetCall call) {
            if (call == TargetCall::Move) {
                Node("A").RemoveComponent("PointerTargetTestComponent"_intern);
            }
        };

        EXPECT_TRUE(Move(kInside, kInside));

        EXPECT_EQ(Calls(_a), Vector<TargetCall>({TargetCall::Enter, TargetCall::Move}));
        EXPECT_FALSE(Move(kInside, kInside));
    }

    TEST_F(PointerTargetComponentTest, ASceneResetInsideTheExitCallbackDoesNotCrash) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        ASSERT_TRUE(Move(kInside, kInside));
        _a.hook = [&](TargetCall call) {
            if (call == TargetCall::Exit) {
                _manager.Reset();
            }
        };

        EXPECT_FALSE(Move(kOutside, kOutside));

        EXPECT_EQ(_a.Count(TargetCall::Exit), 1u);
    }

    TEST_F(PointerTargetComponentTest, ASceneResetInsideTheDragCallbackDoesNotCrash) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        ASSERT_TRUE(Press(kInside, kInside));
        _a.hook = [&](TargetCall call) {
            if (call == TargetCall::Drag) {
                _manager.Reset();
            }
        };

        EXPECT_TRUE(Move(kInside + 1.0f, kInside, kMouse, SDL_BUTTON_LMASK));

        EXPECT_EQ(_a.Count(TargetCall::Drag), 1u);
    }

    TEST_F(PointerTargetComponentTest, ADeclinedMotionWhoseEnterDestroyedTheTargetGivesNoExit) {
        ASSERT_NO_FATAL_FAILURE(LoadNodes(TargetNode("A", 0, 0, kBoxSize, kBoxSize)));
        Draw();
        Bind("A", _a);
        _a.takeMove = false;
        _a.hook = [&](TargetCall call) {
            if (call == TargetCall::Move) {
                _manager.Reset();
            }
        };

        Move(kInside, kInside);

        EXPECT_EQ(Calls(_a), Vector<TargetCall>({TargetCall::Enter, TargetCall::Move}));
    }

}  // namespace BECore::Tests
