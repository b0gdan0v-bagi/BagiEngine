#include <BECore/Input/UIInput.h>
#include <Common/Printers.h>
#include <CoreSDL/SDLEvents.h>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        constexpr float kFarAway = 1000.0f;

        /// A handler with a horizontal shape [minX, maxX]; each kind of event answers its own flag and logs what reaches it
        class ShapeHandler : public IUIInputHandler {
        public:
            ShapeHandler(Vector<int>* callLog, int id) : _callLog(callLog), _id(id) {}

            [[nodiscard]] bool ContainsPoint(float x, float /*y*/) const override {
                return x >= minX && x <= maxX;
            }

            bool OnMouseButtonDown(const SDL_MouseButtonEvent& event) override {
                return Handle(downs, event.x, onDown, consumeDown);
            }

            bool OnMouseButtonUp(const SDL_MouseButtonEvent& event) override {
                return Handle(ups, event.x, onUp, consumeUp);
            }

            bool OnMouseMotion(const SDL_MouseMotionEvent& event) override {
                return Handle(motions, event.x, onMotion, consumeMotion);
            }

            bool OnMouseWheel(const SDL_MouseWheelEvent& event) override {
                return Handle(wheels, event.mouse_x, onWheel, consumeWheel);
            }

            void OnPointerLeave(uint32_t which) override {
                leaves.push_back(which);
                if (onLeave) {
                    onLeave();
                }
            }

            void ConsumeAll() {
                consumeDown = consumeUp = consumeMotion = consumeWheel = true;
            }

            float minX = std::numeric_limits<float>::lowest();
            float maxX = std::numeric_limits<float>::max();
            bool consumeDown = false;
            bool consumeUp = false;
            bool consumeMotion = false;
            bool consumeWheel = false;
            MoveOnlyFunction<void()> onDown;
            MoveOnlyFunction<void()> onUp;
            MoveOnlyFunction<void()> onMotion;
            MoveOnlyFunction<void()> onWheel;
            MoveOnlyFunction<void()> onLeave;
            Vector<float> downs;
            Vector<float> ups;
            Vector<float> motions;
            Vector<float> wheels;
            Vector<uint32_t> leaves;

        private:
            bool Handle(Vector<float>& list, float value, MoveOnlyFunction<void()>& hook, bool answer) {
                list.push_back(value);
                if (_callLog != nullptr) {
                    _callLog->push_back(_id);
                }
                if (hook) {
                    hook();
                }
                return answer;
            }

            Vector<int>* _callLog;
            int _id;
        };

        class GameRecorder : public SubscriptionHolder {
        public:
            GameRecorder() {
                Subscribe<SDLEvents::MouseButtonDownEvent, &GameRecorder::OnDown>(this);
                Subscribe<SDLEvents::MouseButtonUpEvent, &GameRecorder::OnUp>(this);
                Subscribe<SDLEvents::MouseMotionEvent, &GameRecorder::OnMotion>(this);
            }

            Vector<float> downs;
            Vector<float> ups;
            Vector<float> motions;

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
        };

        SDL_MouseButtonEvent Button(float x, uint32_t which = 1, uint8_t button = SDL_BUTTON_LEFT) {
            SDL_MouseButtonEvent event{};
            event.which = which;
            event.button = button;
            event.x = x;
            return event;
        }

        SDL_MouseMotionEvent Motion(float x, uint32_t which = 1) {
            SDL_MouseMotionEvent event{};
            event.which = which;
            event.x = x;
            return event;
        }

        SDL_MouseWheelEvent Wheel(float amount, float pointerX) {
            SDL_MouseWheelEvent event{};
            event.x = kFarAway;
            event.y = amount;
            event.mouse_x = pointerX;
            return event;
        }

    }  // namespace

    class UIInputPointerTest : public ::testing::Test {
    protected:
        void TearDown() override {
            for (auto* handler : _registered) {
                _input.Unregister(*handler);
            }
        }

        void Register(ShapeHandler& handler, int order) {
            _input.Register(handler, order);
            _registered.push_back(&handler);
        }

        UIInput _input;
        GameRecorder _game;
        Vector<int> _callLog;
        Vector<IUIInputHandler*> _registered;
    };

    TEST_F(UIInputPointerTest, AHandlerOutsideItsShapeIsSkippedAndTheOneBelowGetsTheEvent) {
        ShapeHandler front(&_callLog, 2);
        ShapeHandler back(&_callLog, 1);
        front.maxX = 10.0f;
        front.ConsumeAll();
        back.ConsumeAll();
        Register(front, 5);
        Register(back, 1);

        EXPECT_TRUE(_input.DispatchMouseButtonDown(Button(50.0f)));
        EXPECT_TRUE(front.downs.empty());
        EXPECT_EQ(back.downs, Vector<float>({50.0f}));

        EXPECT_TRUE(_input.DispatchMouseButtonDown(Button(5.0f)));
        EXPECT_EQ(front.downs, Vector<float>({5.0f}));
        EXPECT_EQ(back.downs.size(), 1u);
    }

    TEST_F(UIInputPointerTest, AHandlerWhoseShapeHoldsNoPointLeavesEveryEventToTheGame) {
        ShapeHandler handler(&_callLog, 1);
        handler.minX = 10.0f;
        handler.maxX = 5.0f;
        handler.ConsumeAll();
        Register(handler, 0);

        EXPECT_FALSE(_input.DispatchMouseButtonDown(Button(7.0f)));
        EXPECT_FALSE(_input.DispatchMouseMotion(Motion(7.0f)));
        EXPECT_FALSE(_input.DispatchMouseWheel(Wheel(1.0f, 7.0f)));

        EXPECT_TRUE(_callLog.empty());
        EXPECT_EQ(_game.downs, Vector<float>({7.0f}));
        EXPECT_EQ(_game.motions, Vector<float>({7.0f}));
    }

    TEST_F(UIInputPointerTest, TheCaptureSendsUpAndMotionToTheHolderOutsideItsShape) {
        ShapeHandler front(&_callLog, 2);
        ShapeHandler back(&_callLog, 1);
        front.maxX = 10.0f;
        front.consumeDown = true;
        front.consumeUp = true;
        front.consumeMotion = true;
        back.ConsumeAll();
        Register(front, 5);
        Register(back, 1);

        ASSERT_TRUE(_input.DispatchMouseButtonDown(Button(5.0f)));

        EXPECT_TRUE(_input.DispatchMouseMotion(Motion(500.0f)));
        EXPECT_TRUE(_input.DispatchMouseButtonUp(Button(500.0f)));

        EXPECT_EQ(front.motions, Vector<float>({500.0f}));
        EXPECT_EQ(front.ups, Vector<float>({500.0f}));
        EXPECT_TRUE(back.motions.empty());
        EXPECT_TRUE(back.ups.empty());
        EXPECT_TRUE(_game.ups.empty());
        EXPECT_TRUE(_game.motions.empty());
    }

    TEST_F(UIInputPointerTest, TheCaptureEndsWithTheUp) {
        ShapeHandler front(&_callLog, 2);
        ShapeHandler back(&_callLog, 1);
        front.maxX = 10.0f;
        front.consumeDown = true;
        back.ConsumeAll();
        Register(front, 5);
        Register(back, 1);

        ASSERT_TRUE(_input.DispatchMouseButtonDown(Button(5.0f)));
        ASSERT_TRUE(_input.DispatchMouseButtonUp(Button(500.0f)));

        EXPECT_TRUE(_input.DispatchMouseButtonUp(Button(500.0f)));
        EXPECT_EQ(front.ups.size(), 1u);
        EXPECT_EQ(back.ups.size(), 1u);
    }

    TEST_F(UIInputPointerTest, TheUpOfAHeldPressIsConsumedWhateverTheHolderAnswers) {
        ShapeHandler front(&_callLog, 2);
        ShapeHandler back(&_callLog, 1);
        front.consumeDown = true;
        back.ConsumeAll();
        Register(front, 5);
        Register(back, 1);

        ASSERT_TRUE(_input.DispatchMouseButtonDown(Button(5.0f)));

        EXPECT_TRUE(_input.DispatchMouseButtonUp(Button(5.0f)));
        EXPECT_EQ(front.ups.size(), 1u);
        EXPECT_TRUE(back.ups.empty());
        EXPECT_TRUE(_game.ups.empty());
    }

    TEST_F(UIInputPointerTest, AMotionTheHolderDeclinesGoesOnWithoutTheHolder) {
        ShapeHandler holder(&_callLog, 2);
        ShapeHandler other(&_callLog, 1);
        holder.consumeDown = true;
        other.consumeMotion = true;
        Register(holder, 5);
        Register(other, 1);

        ASSERT_TRUE(_input.DispatchMouseButtonDown(Button(5.0f)));

        EXPECT_TRUE(_input.DispatchMouseMotion(Motion(8.0f)));
        EXPECT_EQ(holder.motions.size(), 1u);
        EXPECT_EQ(other.motions.size(), 1u);
        EXPECT_EQ(_callLog, Vector<int>({2, 2, 1}));
    }

    TEST_F(UIInputPointerTest, TwoPointersHoldTwoCapturesApart) {
        ShapeHandler a(&_callLog, 1);
        ShapeHandler b(&_callLog, 2);
        a.maxX = 10.0f;
        b.minX = 20.0f;
        b.maxX = 30.0f;
        a.ConsumeAll();
        b.ConsumeAll();
        Register(a, 1);
        Register(b, 2);

        ASSERT_TRUE(_input.DispatchMouseButtonDown(Button(5.0f, 1)));
        ASSERT_TRUE(_input.DispatchMouseButtonDown(Button(25.0f, 2)));
        a.downs.clear();
        b.downs.clear();

        EXPECT_TRUE(_input.DispatchMouseMotion(Motion(900.0f, 2)));
        EXPECT_TRUE(b.motions.size() == 1u && a.motions.empty());

        EXPECT_TRUE(_input.DispatchMouseButtonUp(Button(900.0f, 1)));
        EXPECT_EQ(a.ups.size(), 1u);
        EXPECT_TRUE(b.ups.empty());

        EXPECT_TRUE(_input.DispatchMouseButtonUp(Button(900.0f, 2)));
        EXPECT_EQ(b.ups.size(), 1u);
        EXPECT_EQ(a.ups.size(), 1u);
    }

    TEST_F(UIInputPointerTest, TheCaptureIsPerButton) {
        ShapeHandler holder(&_callLog, 1);
        ShapeHandler back(&_callLog, 0);
        holder.maxX = 10.0f;
        holder.consumeDown = true;
        back.ConsumeAll();
        Register(holder, 5);
        Register(back, 1);

        ASSERT_TRUE(_input.DispatchMouseButtonDown(Button(5.0f, 1, SDL_BUTTON_LEFT)));

        EXPECT_TRUE(_input.DispatchMouseButtonUp(Button(500.0f, 1, SDL_BUTTON_RIGHT)));
        EXPECT_TRUE(holder.ups.empty());
        EXPECT_EQ(back.ups.size(), 1u);
    }

    TEST_F(UIInputPointerTest, APressNobodyConsumesStartsNoCapture) {
        ShapeHandler handler(&_callLog, 1);
        handler.maxX = 10.0f;
        Register(handler, 0);

        EXPECT_FALSE(_input.DispatchMouseButtonDown(Button(5.0f)));
        EXPECT_EQ(_game.downs, Vector<float>({5.0f}));

        handler.ConsumeAll();
        EXPECT_FALSE(_input.DispatchMouseButtonUp(Button(500.0f)));
        EXPECT_TRUE(handler.ups.empty());
        EXPECT_EQ(_game.ups, Vector<float>({500.0f}));
    }

    TEST_F(UIInputPointerTest, TheLeaveIsSentOnceWhenAnotherHandlerTakesThePointer) {
        ShapeHandler front(&_callLog, 2);
        ShapeHandler back(&_callLog, 1);
        front.maxX = 10.0f;
        front.consumeMotion = true;
        back.consumeMotion = true;
        Register(front, 5);
        Register(back, 1);

        EXPECT_TRUE(_input.DispatchMouseMotion(Motion(5.0f)));
        EXPECT_TRUE(front.leaves.empty());

        EXPECT_TRUE(_input.DispatchMouseMotion(Motion(50.0f)));
        EXPECT_EQ(front.leaves, Vector<uint32_t>({1}));
        EXPECT_TRUE(back.leaves.empty());

        EXPECT_TRUE(_input.DispatchMouseMotion(Motion(60.0f)));
        EXPECT_EQ(front.leaves.size(), 1u);

        EXPECT_TRUE(_input.DispatchMouseMotion(Motion(5.0f)));
        EXPECT_EQ(back.leaves, Vector<uint32_t>({1}));
        EXPECT_EQ(front.leaves.size(), 1u);
    }

    TEST_F(UIInputPointerTest, TheLeaveIsSentWhenThePointerMovesOverNothingAndNeverTwice) {
        ShapeHandler handler(&_callLog, 1);
        handler.maxX = 10.0f;
        handler.consumeMotion = true;
        Register(handler, 0);

        EXPECT_TRUE(_input.DispatchMouseMotion(Motion(5.0f)));
        EXPECT_FALSE(_input.DispatchMouseMotion(Motion(50.0f)));
        EXPECT_FALSE(_input.DispatchMouseMotion(Motion(60.0f)));

        EXPECT_EQ(handler.leaves, Vector<uint32_t>({1}));
        EXPECT_EQ(_game.motions, Vector<float>({50.0f, 60.0f}));
    }

    TEST_F(UIInputPointerTest, TheHoverOfEachPointerIsItsOwn) {
        ShapeHandler a(&_callLog, 1);
        ShapeHandler b(&_callLog, 2);
        a.maxX = 10.0f;
        b.minX = 20.0f;
        b.maxX = 30.0f;
        a.consumeMotion = true;
        b.consumeMotion = true;
        Register(a, 1);
        Register(b, 2);

        _input.DispatchMouseMotion(Motion(5.0f, 1));
        _input.DispatchMouseMotion(Motion(25.0f, 2));
        _input.DispatchMouseMotion(Motion(500.0f, 2));

        EXPECT_TRUE(a.leaves.empty());
        EXPECT_EQ(b.leaves, Vector<uint32_t>({2}));
    }

    TEST_F(UIInputPointerTest, ThePointerOverTheHolderKeepsItsHoverAfterTheUp) {
        ShapeHandler holder(&_callLog, 1);
        holder.maxX = 10.0f;
        holder.consumeDown = true;
        holder.consumeMotion = true;
        Register(holder, 0);

        ASSERT_TRUE(_input.DispatchMouseButtonDown(Button(5.0f)));
        EXPECT_TRUE(_input.DispatchMouseMotion(Motion(500.0f)));
        EXPECT_TRUE(holder.leaves.empty());

        _input.DispatchMouseButtonUp(Button(500.0f));
        EXPECT_FALSE(_input.DispatchMouseMotion(Motion(500.0f)));
        EXPECT_EQ(holder.leaves, Vector<uint32_t>({1}));
    }

    TEST_F(UIInputPointerTest, AnUnregisteredHandlerGetsNoLeaveAndNoUp) {
        ShapeHandler handler(&_callLog, 1);
        handler.ConsumeAll();
        Register(handler, 0);
        ASSERT_TRUE(_input.DispatchMouseMotion(Motion(5.0f)));
        ASSERT_TRUE(_input.DispatchMouseButtonDown(Button(5.0f)));

        _input.Unregister(handler);

        EXPECT_FALSE(_input.DispatchMouseMotion(Motion(50.0f)));
        EXPECT_FALSE(_input.DispatchMouseButtonUp(Button(50.0f)));
        EXPECT_TRUE(handler.leaves.empty());
        EXPECT_TRUE(handler.ups.empty());
        EXPECT_EQ(handler.motions.size(), 1u);
        EXPECT_EQ(_game.ups, Vector<float>({50.0f}));
    }

    TEST_F(UIInputPointerTest, AHandlerThatUnregistersItselfWhileConsumingIsNotRememberedAsHoverOrHolder) {
        ShapeHandler self(&_callLog, 1);
        ShapeHandler other(&_callLog, 2);
        self.ConsumeAll();
        other.ConsumeAll();
        self.onDown = [&] { _input.Unregister(self); };
        Register(self, 5);
        Register(other, 1);

        EXPECT_TRUE(_input.DispatchMouseButtonDown(Button(5.0f)));
        EXPECT_EQ(self.downs.size(), 1u);

        EXPECT_TRUE(_input.DispatchMouseButtonUp(Button(5.0f)));
        EXPECT_TRUE(self.ups.empty());
        EXPECT_EQ(other.ups.size(), 1u);

        self.onDown = nullptr;
        self.onMotion = [&] { _input.Unregister(self); };
        _input.Register(self, 5);
        EXPECT_TRUE(_input.DispatchMouseMotion(Motion(5.0f)));
        EXPECT_TRUE(_input.DispatchMouseMotion(Motion(6.0f)));
        EXPECT_TRUE(self.leaves.empty());
        EXPECT_EQ(self.motions.size(), 1u);
        EXPECT_EQ(other.motions.size(), 1u);
    }

    TEST_F(UIInputPointerTest, TheHolderMayUnregisterInsideItsOwnUp) {
        ShapeHandler holder(&_callLog, 1);
        ShapeHandler back(&_callLog, 2);
        holder.maxX = 10.0f;
        holder.consumeDown = true;
        back.ConsumeAll();
        holder.onUp = [&] { _input.Unregister(holder); };
        Register(holder, 5);
        Register(back, 1);

        ASSERT_TRUE(_input.DispatchMouseButtonDown(Button(5.0f)));

        EXPECT_TRUE(_input.DispatchMouseButtonUp(Button(5.0f)));
        EXPECT_FALSE(_input.IsRegistered(holder));
        EXPECT_EQ(holder.ups.size(), 1u);
        EXPECT_TRUE(back.ups.empty());

        EXPECT_TRUE(_input.DispatchMouseMotion(Motion(5.0f)));
        EXPECT_TRUE(_input.DispatchMouseButtonUp(Button(5.0f)));
        EXPECT_EQ(holder.motions.size(), 0u);
        EXPECT_EQ(holder.ups.size(), 1u);
        EXPECT_EQ(back.motions.size(), 1u);
        EXPECT_EQ(back.ups.size(), 1u);
    }

    TEST_F(UIInputPointerTest, ALeaveMayUnregisterOtherHandlersAndItself) {
        ShapeHandler front(&_callLog, 1);
        ShapeHandler back(&_callLog, 2);
        ShapeHandler below(&_callLog, 3);
        front.maxX = 10.0f;
        front.consumeMotion = true;
        back.consumeMotion = true;
        below.consumeMotion = true;
        front.onLeave = [&] {
            _input.Unregister(below);
            _input.Unregister(front);
        };
        Register(front, 5);
        Register(back, 3);
        Register(below, 1);
        ASSERT_TRUE(_input.DispatchMouseMotion(Motion(5.0f)));

        EXPECT_TRUE(_input.DispatchMouseMotion(Motion(50.0f)));

        EXPECT_EQ(front.leaves, Vector<uint32_t>({1}));
        EXPECT_FALSE(_input.IsRegistered(front));
        EXPECT_FALSE(_input.IsRegistered(below));
        EXPECT_TRUE(_input.IsRegistered(back));
        EXPECT_TRUE(_input.DispatchMouseMotion(Motion(5.0f)));
        EXPECT_EQ(front.motions.size(), 1u);
        EXPECT_EQ(back.motions.size(), 2u);
    }

    TEST_F(UIInputPointerTest, AHandlerRegisteredDuringADispatchJoinsAfterIt) {
        ShapeHandler first(&_callLog, 1);
        ShapeHandler newcomer(&_callLog, 2);
        first.consumeDown = false;
        newcomer.ConsumeAll();
        first.onDown = [&] { _input.Register(newcomer, 100); };
        Register(first, 0);
        _registered.push_back(&newcomer);

        EXPECT_FALSE(_input.DispatchMouseButtonDown(Button(1.0f)));
        EXPECT_TRUE(newcomer.downs.empty());
        EXPECT_TRUE(_input.IsRegistered(newcomer));

        first.onDown = nullptr;
        EXPECT_TRUE(_input.DispatchMouseButtonDown(Button(2.0f)));
        EXPECT_EQ(newcomer.downs, Vector<float>({2.0f}));
        EXPECT_EQ(first.downs.size(), 1u);
    }

    TEST_F(UIInputPointerTest, TheWheelAsksTheShapeAtThePointerNotAtTheScrollAmount) {
        ShapeHandler handler(&_callLog, 1);
        handler.maxX = 10.0f;
        handler.consumeWheel = true;
        Register(handler, 0);

        EXPECT_TRUE(_input.DispatchMouseWheel(Wheel(1.0f, 5.0f)));
        EXPECT_EQ(handler.wheels, Vector<float>({5.0f}));

        EXPECT_FALSE(_input.DispatchMouseWheel(Wheel(1.0f, 50.0f)));
        EXPECT_EQ(handler.wheels.size(), 1u);
    }

    TEST_F(UIInputPointerTest, MorePressesThanTheTrackerHoldsStillWorkByTheWalk) {
        ShapeHandler handler(&_callLog, 1);
        handler.ConsumeAll();
        Register(handler, 0);

        for (uint32_t which = 1; which <= PointerTracker::kMaxEntries + 1; ++which) {
            EXPECT_TRUE(_input.DispatchMouseButtonDown(Button(1.0f, which)));
        }
        for (uint32_t which = 1; which <= PointerTracker::kMaxEntries + 1; ++which) {
            EXPECT_TRUE(_input.DispatchMouseButtonUp(Button(1.0f, which)));
        }

        EXPECT_EQ(handler.ups.size(), PointerTracker::kMaxEntries + 1);
        EXPECT_TRUE(_game.ups.empty());
    }

}  // namespace BECore::Tests
