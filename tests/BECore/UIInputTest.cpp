#include <BECore/Input/UIInput.h>
#include <Common/Printers.h>
#include <CoreSDL/SDLEvents.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// Records what reaches it and answers with a fixed verdict; an optional hook runs inside the call
        class RecordingHandler : public IUIInputHandler {
        public:
            RecordingHandler(Vector<int>* callLog, int id) : _callLog(callLog), _id(id) {}

            bool OnMouseButtonDown(const SDL_MouseButtonEvent& event) override {
                return Handle(downs, event.x);
            }

            bool OnMouseButtonUp(const SDL_MouseButtonEvent& event) override {
                return Handle(ups, event.x);
            }

            bool OnMouseMotion(const SDL_MouseMotionEvent& event) override {
                return Handle(motions, event.x);
            }

            bool OnMouseWheel(const SDL_MouseWheelEvent& event) override {
                return Handle(wheels, event.y);
            }

            bool consume = false;
            eastl::function<void()> hook;
            Vector<float> downs;
            Vector<float> ups;
            Vector<float> motions;
            Vector<float> wheels;

        private:
            bool Handle(Vector<float>& list, float value) {
                list.push_back(value);
                if (_callLog != nullptr) {
                    _callLog->push_back(_id);
                }
                if (hook) {
                    hook();
                }
                return consume;
            }

            Vector<int>* _callLog;
            int _id;
        };

        /// What reaches game code through the SDLEvents events while it is alive
        class GameRecorder : public SubscriptionHolder {
        public:
            GameRecorder() {
                Subscribe<SDLEvents::MouseButtonDownEvent, &GameRecorder::OnDown>(this);
                Subscribe<SDLEvents::MouseButtonUpEvent, &GameRecorder::OnUp>(this);
                Subscribe<SDLEvents::MouseMotionEvent, &GameRecorder::OnMotion>(this);
                Subscribe<SDLEvents::MouseWheelEvent, &GameRecorder::OnWheel>(this);
            }

            Vector<float> downs;
            Vector<float> ups;
            Vector<float> motions;
            Vector<float> wheels;

        private:
            void OnDown(const SDLEvents::MouseButtonDownEvent& event) {
                downs.push_back(event.button.x);
            }

            void OnUp(const SDLEvents::MouseButtonUpEvent& event) {
                ups.push_back(event.button.x);
            }

            void OnMotion(const SDLEvents::MouseMotionEvent& event) {
                motions.push_back(event.motion.x);
            }

            void OnWheel(const SDLEvents::MouseWheelEvent& event) {
                wheels.push_back(event.wheel.y);
            }
        };

        SDL_MouseButtonEvent ButtonAt(float x) {
            SDL_MouseButtonEvent event{};
            event.button = SDL_BUTTON_LEFT;
            event.x = x;
            return event;
        }

        SDL_MouseMotionEvent MotionAt(float x) {
            SDL_MouseMotionEvent event{};
            event.x = x;
            return event;
        }

        SDL_MouseWheelEvent WheelBy(float y) {
            SDL_MouseWheelEvent event{};
            event.y = y;
            return event;
        }

    }  // namespace

    class UIInputTest : public ::testing::Test {
    protected:
        void TearDown() override {
            // A failed test must not leave a handler behind: the next one would see it
            for (auto* handler : _registered) {
                _input.Unregister(*handler);
            }
        }

        void Register(RecordingHandler& handler, int order) {
            _input.Register(handler, order);
            _registered.push_back(&handler);
        }

        UIInput _input;
        GameRecorder _game;
        Vector<int> _callLog;
        Vector<IUIInputHandler*> _registered;
    };

    TEST_F(UIInputTest, WithoutHandlersEveryEventReachesTheGame) {
        EXPECT_FALSE(_input.DispatchMouseButtonDown(ButtonAt(1.0f)));
        EXPECT_FALSE(_input.DispatchMouseButtonUp(ButtonAt(2.0f)));
        EXPECT_FALSE(_input.DispatchMouseMotion(MotionAt(3.0f)));
        EXPECT_FALSE(_input.DispatchMouseWheel(WheelBy(4.0f)));

        EXPECT_EQ(_game.downs, Vector<float>({1.0f}));
        EXPECT_EQ(_game.ups, Vector<float>({2.0f}));
        EXPECT_EQ(_game.motions, Vector<float>({3.0f}));
        EXPECT_EQ(_game.wheels, Vector<float>({4.0f}));
    }

    TEST_F(UIInputTest, AHandlerThatDeclinesLeavesTheEventToTheGame) {
        RecordingHandler handler(&_callLog, 1);
        Register(handler, 0);

        EXPECT_FALSE(_input.DispatchMouseButtonDown(ButtonAt(10.0f)));

        EXPECT_EQ(handler.downs, Vector<float>({10.0f}));
        EXPECT_EQ(_game.downs, Vector<float>({10.0f}));
    }

    TEST_F(UIInputTest, AConsumedEventDoesNotReachTheGame) {
        RecordingHandler handler(&_callLog, 1);
        handler.consume = true;
        Register(handler, 0);

        EXPECT_TRUE(_input.DispatchMouseButtonDown(ButtonAt(1.0f)));
        EXPECT_TRUE(_input.DispatchMouseButtonUp(ButtonAt(2.0f)));
        EXPECT_TRUE(_input.DispatchMouseMotion(MotionAt(3.0f)));
        EXPECT_TRUE(_input.DispatchMouseWheel(WheelBy(4.0f)));

        EXPECT_EQ(handler.downs.size(), 1u);
        EXPECT_EQ(handler.ups.size(), 1u);
        EXPECT_EQ(handler.motions.size(), 1u);
        EXPECT_EQ(handler.wheels.size(), 1u);
        EXPECT_TRUE(_game.downs.empty());
        EXPECT_TRUE(_game.ups.empty());
        EXPECT_TRUE(_game.motions.empty());
        EXPECT_TRUE(_game.wheels.empty());
    }

    TEST_F(UIInputTest, TheHigherOrderIsAskedFirst) {
        RecordingHandler back(&_callLog, 1);
        RecordingHandler middle(&_callLog, 2);
        RecordingHandler front(&_callLog, 3);
        Register(middle, 5);
        Register(back, -1);
        Register(front, 10);

        _input.DispatchMouseButtonDown(ButtonAt(0.0f));

        EXPECT_EQ(_callLog, Vector<int>({3, 2, 1}));
    }

    TEST_F(UIInputTest, AmongEqualOrdersTheLaterRegisteredIsAskedFirst) {
        RecordingHandler first(&_callLog, 1);
        RecordingHandler second(&_callLog, 2);
        Register(first, 4);
        Register(second, 4);

        _input.DispatchMouseButtonDown(ButtonAt(0.0f));

        EXPECT_EQ(_callLog, Vector<int>({2, 1}));
    }

    TEST_F(UIInputTest, TheWalkStopsAtTheFirstHandlerThatConsumes) {
        RecordingHandler back(&_callLog, 1);
        RecordingHandler modal(&_callLog, 2);
        RecordingHandler front(&_callLog, 3);
        modal.consume = true;
        Register(back, 0);
        Register(modal, 1);
        Register(front, 2);

        EXPECT_TRUE(_input.DispatchMouseButtonDown(ButtonAt(0.0f)));

        EXPECT_EQ(_callLog, Vector<int>({3, 2}));
        EXPECT_TRUE(back.downs.empty());
        EXPECT_TRUE(_game.downs.empty());
    }

    TEST_F(UIInputTest, AnUnregisteredHandlerIsNotAskedAgain) {
        RecordingHandler handler(&_callLog, 1);
        Register(handler, 0);
        EXPECT_TRUE(_input.IsRegistered(handler));

        _input.Unregister(handler);

        EXPECT_FALSE(_input.IsRegistered(handler));
        _input.DispatchMouseButtonDown(ButtonAt(0.0f));
        EXPECT_TRUE(handler.downs.empty());
        _input.Unregister(handler);  // a second call is ignored
    }

    TEST_F(UIInputTest, AHandlerUnregisteredByAnEarlierOneInTheSameDispatchIsSkipped) {
        RecordingHandler lower(&_callLog, 1);
        RecordingHandler upper(&_callLog, 2);
        upper.hook = [&]() { _input.Unregister(lower); };
        Register(lower, 0);
        Register(upper, 1);

        _input.DispatchMouseButtonDown(ButtonAt(0.0f));

        EXPECT_EQ(_callLog, Vector<int>({2}));
        EXPECT_FALSE(_input.IsRegistered(lower));
        EXPECT_EQ(_game.downs.size(), 1u);
    }

    TEST_F(UIInputTest, AHandlerMayUnregisterItselfInsideTheCall) {
        RecordingHandler self(&_callLog, 1);
        RecordingHandler other(&_callLog, 2);
        self.hook = [&]() { _input.Unregister(self); };
        Register(other, 0);
        Register(self, 1);

        _input.DispatchMouseButtonDown(ButtonAt(0.0f));
        _input.DispatchMouseButtonDown(ButtonAt(0.0f));

        EXPECT_EQ(_callLog, Vector<int>({1, 2, 2}));
    }

    TEST_F(UIInputTest, AHandlerRegisteredInsideTheCallJoinsAfterTheDispatch) {
        RecordingHandler late(&_callLog, 2);
        RecordingHandler adder(&_callLog, 1);
        adder.hook = [&]() {
            if (!_input.IsRegistered(late)) {
                Register(late, 10);
            }
        };
        Register(adder, 0);

        _input.DispatchMouseButtonDown(ButtonAt(0.0f));
        EXPECT_TRUE(late.downs.empty());
        EXPECT_TRUE(_input.IsRegistered(late));

        _input.DispatchMouseButtonDown(ButtonAt(0.0f));
        EXPECT_EQ(_callLog, Vector<int>({1, 2, 1}));
    }

    TEST_F(UIInputTest, AHandlerThatLeavesInsideTheCallBeforeItJoinedNeverRuns) {
        RecordingHandler late(&_callLog, 2);
        RecordingHandler adder(&_callLog, 1);
        adder.hook = [&]() {
            Register(late, 10);
            _input.Unregister(late);
        };
        Register(adder, 0);

        _input.DispatchMouseButtonDown(ButtonAt(0.0f));
        _input.DispatchMouseButtonDown(ButtonAt(0.0f));

        EXPECT_TRUE(late.downs.empty());
        EXPECT_FALSE(_input.IsRegistered(late));
    }

}  // namespace BECore::Tests
