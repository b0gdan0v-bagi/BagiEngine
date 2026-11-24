#include <BECore/Control/ControlService.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/InputControl.h>
#include <BECore/Input/UIInput.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <CoreSDL/SDLEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr double kTolerance = 0.001;
        constexpr float kHandlerLeft = 100.0f;
        constexpr float kHandlerRight = 300.0f;
        constexpr uint32_t kOtherPointer = 7;

        /// Takes or declines the wheel inside a horizontal band and remembers the last event
        class WheelHandler : public IUIInputHandler, public SubscriptionHolder {
        public:
            explicit WheelHandler(bool takes) : takes(takes) {
                Subscribe<SDLEvents::MouseWheelEvent, &WheelHandler::OnRaw>(this);
            }

            bool ContainsPoint(float x, float /*y*/) const override {
                return x >= kHandlerLeft && x < kHandlerRight;
            }

            bool OnMouseWheel(const SDL_MouseWheelEvent& event) override {
                last = event;
                ++handled;
                return takes;
            }

            bool takes;
            int handled = 0;
            int raw = 0;
            SDL_MouseWheelEvent last{};

        private:
            void OnRaw(const SDLEvents::MouseWheelEvent& /*event*/) {
                ++raw;
            }
        };

    }  // namespace

    class WheelAtControlTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            RegisterInputControl(_service);
        }

        void TearDown() override {
            if (_handler != nullptr) {
                CoreManager::GetUIInput().Unregister(*_handler);
                _handler = nullptr;
            }
        }

        WheelHandler& Install(bool takes) {
            _handlerStorage = MakeUnique<WheelHandler>(takes);
            _handler = _handlerStorage.get();
            CoreManager::GetUIInput().Register(*_handler, 0);
            return *_handler;
        }

        ControlReply Call(StringView paramsText) {
            return CallControlText(_service, ++_nextId, "wheel_at", paramsText);
        }

        ControlService _service;
        UniquePtr<WheelHandler> _handlerStorage;
        WheelHandler* _handler = nullptr;
        int64_t _nextId = 0;
    };

    TEST_F(WheelAtControlTest, TheMethodIsListedWithItsParameters) {
        bool listed = false;
        for (const ControlMethodListing& method : _service.GetMethods()) {
            listed = listed || method.name == "wheel_at";
        }
        EXPECT_TRUE(listed);
    }

    TEST_F(WheelAtControlTest, AHandlerGetsThePositionAndTheAmountAsAsked) {
        WheelHandler& handler = Install(true);
        const ControlReply response = Call(R"({"x":150.5,"y":40.0,"dx":2.0,"dy":-3.0,"which":7})");

        ASSERT_TRUE(response.IsOk());
        ASSERT_EQ(handler.handled, 1);
        EXPECT_FLOAT_EQ(handler.last.mouse_x, 150.5f);
        EXPECT_FLOAT_EQ(handler.last.mouse_y, 40.0f);
        EXPECT_FLOAT_EQ(handler.last.x, 2.0f);
        EXPECT_FLOAT_EQ(handler.last.y, -3.0f);
        EXPECT_EQ(handler.last.direction, SDL_MOUSEWHEEL_NORMAL);
        EXPECT_EQ(handler.last.which, kOtherPointer);
        EXPECT_NEAR(NumberOf(response.Result(), "x"), 150.5, kTolerance);
        EXPECT_NEAR(NumberOf(response.Result(), "y"), 40.0, kTolerance);
    }

    TEST_F(WheelAtControlTest, FlippedArrivesAsTheFlippedDirection) {
        WheelHandler& handler = Install(true);

        ASSERT_TRUE(Call(R"({"x":150.0,"y":40.0,"dy":1.0,"flipped":true})").IsOk());

        EXPECT_EQ(handler.last.direction, SDL_MOUSEWHEEL_FLIPPED);
        EXPECT_FLOAT_EQ(handler.last.x, 0.0f) << "an absent amount is 0";
    }

    TEST_F(WheelAtControlTest, AHandlerThatTakesItKeepsItFromGameCode) {
        WheelHandler& handler = Install(true);

        const ControlReply response = Call(R"({"x":150.0,"y":40.0,"dy":1.0})");

        EXPECT_TRUE(BoolOf(response.Result(), "consumed").value_or(false));
        EXPECT_EQ(handler.raw, 0);
    }

    TEST_F(WheelAtControlTest, AHandlerThatDeclinesLetsTheRawEventThrough) {
        WheelHandler& handler = Install(false);

        const ControlReply response = Call(R"({"x":150.0,"y":40.0,"dy":1.0})");

        ASSERT_TRUE(response.IsOk());
        EXPECT_FALSE(BoolOf(response.Result(), "consumed").value_or(true));
        EXPECT_EQ(handler.raw, 1);
    }

    TEST_F(WheelAtControlTest, APointOutsideTheHandlerIsNotAskedAndReachesGameCode) {
        WheelHandler& handler = Install(true);

        const ControlReply response = Call(R"({"x":350.0,"y":40.0,"dy":1.0})");  // right of kHandlerRight

        EXPECT_FALSE(BoolOf(response.Result(), "consumed").value_or(true));
        EXPECT_EQ(handler.handled, 0);
        EXPECT_EQ(handler.raw, 1);
    }

    TEST_F(WheelAtControlTest, BadParametersAreInvalidAndSendNothing) {
        WheelHandler& handler = Install(true);
        // a NaN or an infinity has no JSON text: the wire carries null for it
        const StringView badParams[] = {
            R"({"x":150.0,"dy":1.0})",
            R"({"x":150.0,"y":40.0,"dy":null})",
            R"({"x":150.0,"y":40.0,"dx":null})",
            R"({"x":"left","y":40.0,"dy":1.0})",
            R"({"x":150.0,"y":40.0,"dy":1.0,"flipped":"yes"})",
            R"({"x":150.0,"y":40.0,"dy":1.0,"which":-1})",
            R"({"x":150.0,"y":40.0})",
            R"({"x":150.0,"y":40.0,"dx":"a lot"})",
        };

        for (const StringView params : badParams) {
            const ControlReply response = Call(params);
            EXPECT_FALSE(response.IsOk()) << String(params).c_str();
            EXPECT_EQ(response.ErrorCode(), "invalid_params") << String(params).c_str();
        }
        EXPECT_EQ(handler.handled, 0);
        EXPECT_EQ(handler.raw, 0);
    }

}  // namespace BECore::Tests
