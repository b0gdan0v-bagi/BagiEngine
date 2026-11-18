#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Events/EventBase.h>
#include <TaskSystem/EventAwaiter.h>
#include <TaskSystem/TaskManager.h>
#include <chrono>
#include <coroutine>
#include <limits>
#include <thread>

namespace BECore::Tests {

    namespace {
        struct AwaitedEvent : EventBase<AwaitedEvent> {
            int value = 0;

            explicit AwaitedEvent(int v) : value(v) {}
        };

        // A coroutine that starts at once and frees its own frame at the end: the test needs no scheduler.
        struct DetachedCoroutine {
            std::coroutine_handle<> handle;  // valid only while the coroutine is suspended

            struct promise_type {
                DetachedCoroutine get_return_object() noexcept {
                    return {std::coroutine_handle<promise_type>::from_promise(*this)};
                }
                std::suspend_never initial_suspend() noexcept {
                    return {};
                }
                std::suspend_never final_suspend() noexcept {
                    return {};
                }
                void return_void() noexcept {}
                void unhandled_exception() noexcept {}
            };
        };

        DetachedCoroutine AwaitTwice(Vector<int>& received, bool& finished) {
            const AwaitedEvent first = co_await WaitForEvent<AwaitedEvent>();
            received.push_back(first.value);
            const AwaitedEvent second = co_await WaitForEvent<AwaitedEvent>();
            received.push_back(second.value);
            finished = true;
        }

        DetachedCoroutine AwaitOnce(Vector<int>& received) {
            const AwaitedEvent event = co_await WaitForEvent<AwaitedEvent>();
            received.push_back(event.value);
        }

        struct TimedOutcome {
            int resumes = 0;
            Optional<int> value;
        };

        template <typename TDuration>
        DetachedCoroutine AwaitWithTimeout(TDuration timeout, TimedOutcome& outcome) {
            const Optional<AwaitedEvent> event = co_await WaitForEventWithTimeout<AwaitedEvent>(timeout);
            ++outcome.resumes;
            if (event) {
                outcome.value = event->value;
            }
        }

        class Bystander : public SubscriptionHolder {
        public:
            using SubscriptionHolder::Subscribe;

            void OnAwaited(const AwaitedEvent& e) {
                seen.push_back(e.value);
            }

            Vector<int> seen;
        };
    }  // namespace

    class EventAwaiterTimeoutTest : public ::testing::Test {
    protected:
        static constexpr auto kShortTimeout = std::chrono::milliseconds(1);
        static constexpr auto kLongTimeout = std::chrono::hours(1);
        static constexpr auto kWaitLimit = std::chrono::seconds(30);

        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _delayedBefore = Scheduler().GetDelayedTaskCount();
        }

        static TaskScheduler& Scheduler() {
            return *TaskManager::GetInstance().GetScheduler();
        }

        /// Runs the main-thread stage of the frames until every timeout this test scheduled has come due
        void RunUntilTheTimeoutsAreDue() const {
            const auto limit = std::chrono::steady_clock::now() + kWaitLimit;
            do {
                Scheduler().ProcessDelayedTasks();
                Scheduler().ProcessMainThreadTasks();
                std::this_thread::yield();
            } while (Scheduler().GetDelayedTaskCount() > _delayedBefore && std::chrono::steady_clock::now() < limit);
            Scheduler().ProcessMainThreadTasks();
        }

        size_t _delayedBefore = 0;
    };

    TEST_F(EventAwaiterTimeoutTest, TheTimeRunningOutResumesOnceWithNothingAndALateEventDoesNothing) {
        TimedOutcome outcome;
        AwaitWithTimeout(kShortTimeout, outcome);
        EXPECT_EQ(outcome.resumes, 0);

        RunUntilTheTimeoutsAreDue();
        EXPECT_EQ(outcome.resumes, 1);
        EXPECT_FALSE(outcome.value.has_value());

        AwaitedEvent::Emit(7);
        EXPECT_EQ(outcome.resumes, 1);
        EXPECT_FALSE(outcome.value.has_value());
    }

    TEST_F(EventAwaiterTimeoutTest, AnEventInTimeResumesOnceWithTheEventAndTheLateTimeoutDoesNothing) {
        TimedOutcome outcome;
        AwaitWithTimeout(kShortTimeout, outcome);

        AwaitedEvent::Emit(7);
        EXPECT_EQ(outcome.resumes, 1);
        EXPECT_EQ(outcome.value, Optional<int>(7));

        RunUntilTheTimeoutsAreDue();
        EXPECT_EQ(outcome.resumes, 1);
        EXPECT_EQ(outcome.value, Optional<int>(7));
    }

    TEST_F(EventAwaiterTimeoutTest, ATimeThatIsNotPositiveDoesNotSuspend) {
        TimedOutcome zero;
        AwaitWithTimeout(std::chrono::milliseconds(0), zero);
        EXPECT_EQ(zero.resumes, 1);

        TimedOutcome negative;
        AwaitWithTimeout(std::chrono::milliseconds(-5), negative);
        EXPECT_EQ(negative.resumes, 1);

        TimedOutcome notANumber;
        AwaitWithTimeout(std::chrono::duration<double>(std::numeric_limits<double>::quiet_NaN()), notANumber);
        EXPECT_EQ(notANumber.resumes, 1);

        EXPECT_EQ(Scheduler().GetDelayedTaskCount(), _delayedBefore) << "nothing was scheduled";
    }

    TEST_F(EventAwaiterTimeoutTest, AVeryLongTimeStillWaitsForTheEvent) {
        TimedOutcome outcome;
        AwaitWithTimeout(std::chrono::hours::max(), outcome);

        Scheduler().ProcessDelayedTasks();
        Scheduler().ProcessMainThreadTasks();
        EXPECT_EQ(outcome.resumes, 0) << "the time did not wrap around to the past";

        AwaitedEvent::Emit(3);
        EXPECT_EQ(outcome.resumes, 1);
        EXPECT_EQ(outcome.value, Optional<int>(3));
    }

    TEST_F(EventAwaiterTimeoutTest, DestroyingASuspendedCoroutineLeavesTheTimeoutNothingToResume) {
        TimedOutcome outcome;
        DetachedCoroutine coroutine = AwaitWithTimeout(kShortTimeout, outcome);
        coroutine.handle.destroy();

        RunUntilTheTimeoutsAreDue();
        AwaitedEvent::Emit(1);

        EXPECT_EQ(outcome.resumes, 0);
    }

    TEST_F(EventAwaiterTimeoutTest, EachAwaitHasItsOwnTimeout) {
        TimedOutcome shortWait;
        TimedOutcome longWait;
        AwaitWithTimeout(kShortTimeout, shortWait);
        AwaitWithTimeout(kLongTimeout, longWait);

        const auto limit = std::chrono::steady_clock::now() + kWaitLimit;
        while (shortWait.resumes == 0 && std::chrono::steady_clock::now() < limit) {
            Scheduler().ProcessDelayedTasks();
            Scheduler().ProcessMainThreadTasks();
            std::this_thread::yield();
        }
        EXPECT_EQ(shortWait.resumes, 1);
        EXPECT_EQ(longWait.resumes, 0);

        AwaitedEvent::Emit(9);
        EXPECT_EQ(shortWait.resumes, 1);
        EXPECT_EQ(longWait.value, Optional<int>(9));
    }

    TEST(EventAwaiterTest, TheCoroutineResumesWithTheEventAndEachAwaitTakesOneEmit) {
        Vector<int> received;
        bool finished = false;
        Bystander bystander;
        bystander.Subscribe<AwaitedEvent, &Bystander::OnAwaited>(&bystander);

        AwaitTwice(received, finished);
        EXPECT_TRUE(received.empty()) << "nothing arrives before the event";

        AwaitedEvent::Emit(1);
        EXPECT_EQ(received, (Vector<int>{1}));
        EXPECT_FALSE(finished);

        AwaitedEvent::Emit(2);
        EXPECT_EQ(received, (Vector<int>{1, 2}));
        EXPECT_TRUE(finished);

        // The finished coroutine left nothing behind: an event now reaches only the bystander.
        AwaitedEvent::Emit(3);
        EXPECT_EQ(received, (Vector<int>{1, 2}));
        EXPECT_EQ(bystander.seen, (Vector<int>{1, 2, 3}));
    }

    TEST(EventAwaiterTest, DestroyingASuspendedCoroutineRemovesItsListener) {
        Vector<int> received;
        Bystander bystander;
        bystander.Subscribe<AwaitedEvent, &Bystander::OnAwaited>(&bystander);

        DetachedCoroutine coroutine = AwaitOnce(received);
        coroutine.handle.destroy();

        AwaitedEvent::Emit(5);

        EXPECT_TRUE(received.empty());
        EXPECT_EQ(bystander.seen, (Vector<int>{5}));
    }

}  // namespace BECore::Tests
