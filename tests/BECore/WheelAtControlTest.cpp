#include <BECore/Control/ControlService.h>
#include <BECore/Control/Json.h>
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

        Json Params(double x, double y) {
            Json params = Json::object();
            params["x"] = x;
            params["y"] = y;
            return params;
        }

        const Json& Result(const Json& response) {
            static const Json kEmpty = Json::object();
            const Json* result = FindJsonMember(response, "result");
            return result != nullptr ? *result : kEmpty;
        }

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

        Json Call(const Json& params) {
            return CallControl(_service, ++_nextId, "wheel_at", params);
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
        Json params = Params(150.5, 40.0);
        params["dx"] = 2.0;
        params["dy"] = -3.0;
        params["which"] = kOtherPointer;

        const Json response = Call(params);

        ASSERT_TRUE(IsOk(response));
        ASSERT_EQ(handler.handled, 1);
        EXPECT_FLOAT_EQ(handler.last.mouse_x, 150.5f);
        EXPECT_FLOAT_EQ(handler.last.mouse_y, 40.0f);
        EXPECT_FLOAT_EQ(handler.last.x, 2.0f);
        EXPECT_FLOAT_EQ(handler.last.y, -3.0f);
        EXPECT_EQ(handler.last.direction, SDL_MOUSEWHEEL_NORMAL);
        EXPECT_EQ(handler.last.which, kOtherPointer);
        EXPECT_NEAR(GetJsonNumber(Result(response), "x").value_or(-1.0), 150.5, kTolerance);
        EXPECT_NEAR(GetJsonNumber(Result(response), "y").value_or(-1.0), 40.0, kTolerance);
    }

    TEST_F(WheelAtControlTest, FlippedArrivesAsTheFlippedDirection) {
        WheelHandler& handler = Install(true);
        Json params = Params(150.0, 40.0);
        params["dy"] = 1.0;
        params["flipped"] = true;

        ASSERT_TRUE(IsOk(Call(params)));

        EXPECT_EQ(handler.last.direction, SDL_MOUSEWHEEL_FLIPPED);
        EXPECT_FLOAT_EQ(handler.last.x, 0.0f) << "an absent amount is 0";
    }

    TEST_F(WheelAtControlTest, AHandlerThatTakesItKeepsItFromGameCode) {
        WheelHandler& handler = Install(true);
        Json params = Params(150.0, 40.0);
        params["dy"] = 1.0;

        const Json response = Call(params);

        EXPECT_TRUE(GetJsonBool(Result(response), "consumed").value_or(false));
        EXPECT_EQ(handler.raw, 0);
    }

    TEST_F(WheelAtControlTest, AHandlerThatDeclinesLetsTheRawEventThrough) {
        WheelHandler& handler = Install(false);
        Json params = Params(150.0, 40.0);
        params["dy"] = 1.0;

        const Json response = Call(params);

        ASSERT_TRUE(IsOk(response));
        EXPECT_FALSE(GetJsonBool(Result(response), "consumed").value_or(true));
        EXPECT_EQ(handler.raw, 1);
    }

    TEST_F(WheelAtControlTest, APointOutsideTheHandlerIsNotAskedAndReachesGameCode) {
        WheelHandler& handler = Install(true);
        Json params = Params(kHandlerRight + 50.0, 40.0);
        params["dy"] = 1.0;

        const Json response = Call(params);

        EXPECT_FALSE(GetJsonBool(Result(response), "consumed").value_or(true));
        EXPECT_EQ(handler.handled, 0);
        EXPECT_EQ(handler.raw, 1);
    }

    TEST_F(WheelAtControlTest, BadParametersAreInvalidAndSendNothing) {
        WheelHandler& handler = Install(true);
        const double inf = std::numeric_limits<double>::infinity();
        Json missingY = Json::object();
        missingY["x"] = 150.0;
        missingY["dy"] = 1.0;
        Json nan = Params(150.0, 40.0);
        nan["dy"] = std::numeric_limits<double>::quiet_NaN();
        Json huge = Params(150.0, 40.0);
        huge["dx"] = inf;
        Json badX = Params(150.0, 40.0);
        badX["x"] = "left";
        badX["dy"] = 1.0;
        Json badFlipped = Params(150.0, 40.0);
        badFlipped["dy"] = 1.0;
        badFlipped["flipped"] = "yes";
        Json badWhich = Params(150.0, 40.0);
        badWhich["dy"] = 1.0;
        badWhich["which"] = -1;
        Json noAmount = Params(150.0, 40.0);
        Json badDx = Params(150.0, 40.0);
        badDx["dx"] = "a lot";

        for (const Json& params : {missingY, nan, huge, badX, badFlipped, badWhich, noAmount, badDx}) {
            const Json response = Call(params);
            EXPECT_FALSE(IsOk(response)) << DumpJson(params).c_str();
            EXPECT_EQ(ErrorCode(response), "invalid_params") << DumpJson(params).c_str();
        }
        EXPECT_EQ(handler.handled, 0);
        EXPECT_EQ(handler.raw, 0);
    }

}  // namespace BECore::Tests
