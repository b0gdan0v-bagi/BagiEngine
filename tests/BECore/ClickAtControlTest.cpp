#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/InputControl.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Renderer/RendererManager.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kOutputWidth = 800.0f;
        constexpr float kOutputHeight = 600.0f;
        constexpr double kTolerance = 0.001;
        constexpr double kFitScale = 2.0;      // viewWidth 400, viewHeight 200 in the 800 x 600 output
        constexpr double kFitOffsetY = 100.0;  // the bar above the fitted view
        constexpr uint32_t kOtherPointer = 5;

        constexpr const char* kScenesXml = R"(<SceneManager><scenes>
  <scene type="Scene" name="Plain"><nodes>
    <node name="Button" layer="1"><components>
      <component type="TransformComponent" x="100" y="100" width="200" height="100"/>
      <component type="ClickableComponent" actionName="ClickAtButton"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Fitted" viewWidth="400" viewHeight="200"><nodes>
    <node name="Button" layer="1"><components>
      <component type="TransformComponent" x="100" y="50" width="100" height="50"/>
      <component type="ClickableComponent" actionName="ClickAtFitted"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Clipped"><nodes>
    <node name="Below" layer="0"><components>
      <component type="TransformComponent" x="0" y="0" width="300" height="300"/>
      <component type="ClickableComponent" actionName="ClickAtBelow"/>
    </components></node>
    <node name="Clipper" layer="5"><components>
      <component type="TransformComponent" x="0" y="0" width="100" height="100"/>
      <component type="ClipRectComponent"/>
    </components><children>
      <node name="Inside" layer="6"><components>
        <component type="TransformComponent" x="0" y="0" width="300" height="300"/>
        <component type="ClickableComponent" actionName="ClickAtInside"/>
      </components></node>
    </children></node>
  </nodes></scene>
</scenes></SceneManager>)";

        /// Counts the actions and the raw presses that reach game code
        class Recorder : public SubscriptionHolder {
        public:
            Recorder() {
                Subscribe<InputEvents::ActionEvent, &Recorder::OnAction>(this);
                Subscribe<SDLEvents::MouseButtonDownEvent, &Recorder::OnDown>(this);
            }

            int Count(const char* action) const {
                return static_cast<int>(std::count(_actions.begin(), _actions.end(), PoolString::Intern(action)));
            }
            size_t Total() const {
                return _actions.size();
            }

            int downs = 0;
            uint8_t lastButton = 0;
            uint32_t lastWhich = 0;

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                _actions.push_back(event.action);
            }
            void OnDown(const SDLEvents::MouseButtonDownEvent& event) {
                ++downs;
                lastButton = event.button.button;
                lastWhich = event.button.which;
            }

            Vector<PoolString> _actions;
        };

        double Number(JsonView object, StringView key) {
            return MemberOf(object, key).AsDouble().value_or(-12345.0);
        }

    }  // namespace

    class ClickAtControlTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _renderer = New<RecordingRenderer>();
            _renderer->width = kOutputWidth;
            _renderer->height = kOutputHeight;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);
            RegisterInputControl(_service);
        }

        void TearDown() override {
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
        }

        void Show(const char* sceneName) {
            _manager->SetActiveScene(PoolString::Intern(sceneName));
            _manager->DrawAll();
        }

        /// One request whose params are the text as written: a value no `double` can hold (`1e999`), an escaped key or value and a null stand in the text
        ControlReply Call(StringView paramsText) {
            return CallControlText(_service, ++_nextId, "click_at", paramsText);
        }

        static String At(double x, double y) {
            return Format(R"({{"x":{},"y":{}}})", x, y);
        }

        static JsonView Result(const ControlReply& response) {
            return response.Result();
        }

        static bool Consumed(const ControlReply& response) {
            return BoolOf(Result(response), "consumed").value_or(false);
        }

        ControlService _service;
        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtr<SceneManager> _manager;
        int64_t _nextId = 0;
    };

    TEST_F(ClickAtControlTest, TheMethodIsListedWithItsParameters) {
        bool listed = false;
        for (const ControlMethodListing& method : _service.GetMethods()) {
            listed = listed || method.name == "click_at";
        }
        EXPECT_TRUE(listed);
    }

    TEST_F(ClickAtControlTest, AClickInOutputPixelsAtTheMiddleOfATargetEmitsItsAction) {
        Show("Plain");
        Recorder recorder;

        const ControlReply response = Call(At(200.0, 150.0));

        ASSERT_TRUE(response.IsOk());
        EXPECT_TRUE(Consumed(response));
        EXPECT_EQ(recorder.Count("ClickAtButton"), 1);
        EXPECT_EQ(recorder.downs, 0) << "a target took the press, the game got no raw event";
        EXPECT_NEAR(Number(Result(response), "outputX"), 200.0, kTolerance);
        EXPECT_NEAR(Number(Result(response), "outputY"), 150.0, kTolerance);
    }

    TEST_F(ClickAtControlTest, AClickOnEmptySpaceIsNotConsumedAndReachesGameCode) {
        Show("Plain");
        Recorder recorder;

        const ControlReply response = Call(At(10.0, 10.0));

        ASSERT_TRUE(response.IsOk());
        EXPECT_FALSE(Consumed(response));
        EXPECT_EQ(recorder.Total(), 0u);
        EXPECT_EQ(recorder.downs, 1);
    }

    TEST_F(ClickAtControlTest, TheAnswerEchoesAFloatCoordinateAsTheDoubleItWidensTo) {
        Show("Plain");

        const ControlReply response = Call(At(0.1, 0.1));

        ASSERT_TRUE(response.IsOk());
        EXPECT_EQ(Number(Result(response), "x"), static_cast<double>(0.1f));
        EXPECT_EQ(Number(Result(response), "outputY"), static_cast<double>(0.1f));
    }

    TEST_F(ClickAtControlTest, ASceneUnitClickIsMappedThroughTheViewFitOfTheLastDraw) {
        Show("Fitted");
        Recorder recorder;

        const ControlReply response = Call(R"({"x":150.0,"y":75.0,"space":"scene"})");  // the middle of the button in scene units

        ASSERT_TRUE(response.IsOk());
        EXPECT_TRUE(Consumed(response));
        EXPECT_EQ(recorder.Count("ClickAtFitted"), 1);
        EXPECT_NEAR(Number(Result(response), "x"), 150.0, kTolerance) << "the answer repeats the point as given";
        EXPECT_NEAR(Number(Result(response), "outputX"), 150.0 * kFitScale, kTolerance);
        EXPECT_NEAR(Number(Result(response), "outputY"), 75.0 * kFitScale + kFitOffsetY, kTolerance);
    }

    TEST_F(ClickAtControlTest, TheSamePointInOutputPixelsMissesAFittedTarget) {
        Show("Fitted");
        Recorder recorder;

        const ControlReply response = Call(At(150.0, 75.0));

        ASSERT_TRUE(response.IsOk());
        EXPECT_FALSE(Consumed(response));
        EXPECT_EQ(recorder.Count("ClickAtFitted"), 0);
    }

    TEST_F(ClickAtControlTest, AClickOutsideAClipRectReachesTheTargetBelowAndInsideItTheClippedOne) {
        Show("Clipped");
        Recorder recorder;

        ASSERT_TRUE(Consumed(Call(At(200.0, 200.0))));
        EXPECT_EQ(recorder.Count("ClickAtBelow"), 1);
        EXPECT_EQ(recorder.Count("ClickAtInside"), 0) << "the part of the target outside the clip takes nothing";

        ASSERT_TRUE(Consumed(Call(At(50.0, 50.0))));
        EXPECT_EQ(recorder.Count("ClickAtInside"), 1);
        EXPECT_EQ(recorder.Count("ClickAtBelow"), 1);
    }

    TEST_F(ClickAtControlTest, TheButtonAndThePointerAreThoseOfTheRequest) {
        Show("Plain");
        Recorder recorder;

        ASSERT_TRUE(Call(Format(R"({{"x":10.0,"y":10.0,"button":"right","which":{}}})", kOtherPointer)).IsOk());

        EXPECT_EQ(recorder.downs, 1);
        EXPECT_EQ(recorder.lastButton, SDL_BUTTON_RIGHT);
        EXPECT_EQ(recorder.lastWhich, kOtherPointer);

        EXPECT_TRUE(Call(At(10.0, 10.0)).IsOk());
        EXPECT_EQ(recorder.lastButton, SDL_BUTTON_LEFT) << "left and the control pointer by default";
        EXPECT_EQ(recorder.lastWhich, kControlPointerId);
    }

    TEST_F(ClickAtControlTest, ABadParameterIsRefusedBeforeAnythingIsPressed) {
        Show("Plain");
        Recorder recorder;
        // an infinity has no JSON text: the wire carries null for it
        const StringView badParams[] = {
            R"({"x":null,"y":150.0})",
            R"({"x":200.0,"y":1000000000000.0})",
            R"({"x":200.0})",
            R"({"x":"200","y":150.0})",
            R"({"x":200.0,"y":150.0,"space":"world"})",
            R"({"x":200.0,"y":150.0,"button":"left-ish"})",
            R"({"x":200.0,"y":150.0,"which":-1})",
            R"({"x":200.0,"y":150.0,"which":1.5})",
        };

        for (const StringView params : badParams) {
            EXPECT_EQ(Call(params).ErrorCode(), "invalid_params") << String(params).c_str();
        }
        EXPECT_EQ(recorder.Total(), 0u);
        EXPECT_EQ(recorder.downs, 0);
    }

    TEST_F(ClickAtControlTest, TextThatNoDoubleCanHoldIsReadByTheTape) {
        Show("Plain");
        Recorder recorder;

        EXPECT_EQ(Call(R"({"x":1e999,"y":150})").ErrorCode(), "invalid_params") << "a coordinate past a double is refused as not finite, not as a parse error";

        ASSERT_TRUE(Call(R"({"x":10,"y":10,"button":"r\u0069ght","which":-0})").IsOk());
        EXPECT_EQ(recorder.lastButton, SDL_BUTTON_RIGHT) << "an escape in a string value is decoded";
        EXPECT_EQ(recorder.lastWhich, 0u) << "-0 is the pointer 0";
    }

    TEST_F(ClickAtControlTest, ASceneUnitClickNeedsAnActiveScene) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString("<SceneManager><scenes/></SceneManager>"));
        auto bare = New<SceneManager>();
        bare->Deserialize(deserializer);
        bare->Initialize();
        ASSERT_EQ(bare->GetActiveScene(), nullptr);
        EngineEnvironment::UseSceneManager(bare);

        EXPECT_EQ(Call(R"({"x":10.0,"y":10.0,"space":"scene"})").ErrorCode(), "no_scene");
        EXPECT_TRUE(Call(At(10.0, 10.0)).IsOk()) << "a click in output pixels needs no scene";
    }

}  // namespace BECore::Tests
