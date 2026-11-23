#include <BECore/Animation/TweenManager.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Wait.h>
#include <Events/EventBase.h>
#include <Events/SubscriptionHolder.h>
#include <TaskSystem/EventAwaiter.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/TaskScheduler.h>
#include <chrono>
#include <coroutine>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {
        struct FirstEvent : EventBase<FirstEvent> {};

        struct SecondEvent : EventBase<SecondEvent> {};

        struct AwaitedEvent : EventBase<AwaitedEvent> {};

        class Listener : public SubscriptionHolder {
        public:
            void SubscribeToBoth() {
                Subscribe<FirstEvent, &Listener::OnFirst>(this);
                Subscribe<SecondEvent, &Listener::OnSecond>(this);
            }

            int calls = 0;

        private:
            void OnFirst() {
                ++calls;
            }
            void OnSecond() {
                ++calls;
            }
        };

        constexpr auto kShortTimeout = std::chrono::milliseconds(1);
    }  // namespace

    // The channel keeps its list across a disconnect, so a listener that came and went leaves the room the case below needs
    TEST(SubscriptionAllocationTest, TheFirstTwoSubscriptionsOfAHolderAllocateNothing) {
        {
            Listener warmUp;
            warmUp.SubscribeToBoth();
        }

        Listener listener;
        ExpectNoAllocations([&listener] { listener.SubscribeToBoth(); });
    }

    TEST(SubscriptionAllocationTest, AnEventAwaiterSuspendsAndResumesWithoutAllocating) {
        {
            EventAwaiter<AwaitedEvent> warmUp;
            warmUp.await_suspend(std::noop_coroutine());
            AwaitedEvent::Emit();
        }

        ExpectNoAllocations([] {
            EventAwaiter<AwaitedEvent> awaiter;
            awaiter.await_suspend(std::noop_coroutine());
            AwaitedEvent::Emit();
        });
    }

    class TimedAwaiterAllocationTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        static TaskScheduler& Scheduler() {
            return *TaskManager::GetInstance().GetScheduler();
        }

        /// Runs the main-thread stage of the frames until every delayed task is due and has run
        static void RunUntilTheTimeoutsAreDue(size_t delayedBefore) {
            WaitUntil([delayedBefore] {
                Scheduler().ProcessDelayedTasks();
                Scheduler().ProcessMainThreadTasks();
                return Scheduler().GetDelayedTaskCount() <= delayedBefore;
            });
            Scheduler().ProcessMainThreadTasks();
        }

        /// Gives the scheduler's delayed queue its capacity, which the awaiters then find in place
        static void WarmUpTheDelayedQueue() {
            const size_t delayedBefore = Scheduler().GetDelayedTaskCount();
            EventAwaiterWithTimeout<AwaitedEvent> warmUp{kShortTimeout};
            warmUp.await_suspend(std::noop_coroutine());
            RunUntilTheTimeoutsAreDue(delayedBefore);
        }
    };

    TEST_F(TimedAwaiterAllocationTest, ATimedAwaitEndedByTheEventGivesItsLinkBack) {
        WarmUpTheDelayedQueue();

        const size_t delayedBefore = Scheduler().GetDelayedTaskCount();
        const size_t linksBefore = Detail::TimerLink::PooledPool().GetLiveCount();
        {
            EventAwaiterWithTimeout<AwaitedEvent> awaiter{kShortTimeout};
            ExpectNoAllocations([&awaiter] { awaiter.await_suspend(std::noop_coroutine()); });
            AwaitedEvent::Emit();
        }
        // The scheduled job still holds the link until its time is due
        RunUntilTheTimeoutsAreDue(delayedBefore);
        EXPECT_EQ(Detail::TimerLink::PooledPool().GetLiveCount(), linksBefore);
    }

    TEST_F(TimedAwaiterAllocationTest, ATimedAwaitEndedByTheTimeoutGivesItsLinkBack) {
        WarmUpTheDelayedQueue();

        const size_t delayedBefore = Scheduler().GetDelayedTaskCount();
        const size_t linksBefore = Detail::TimerLink::PooledPool().GetLiveCount();
        EventAwaiterWithTimeout<AwaitedEvent> awaiter{kShortTimeout};
        ExpectNoAllocations([&awaiter] { awaiter.await_suspend(std::noop_coroutine()); });
        RunUntilTheTimeoutsAreDue(delayedBefore);
        EXPECT_EQ(Detail::TimerLink::PooledPool().GetLiveCount(), linksBefore);
    }

    TEST_F(TimedAwaiterAllocationTest, ATimedAwaitEndedByADestroyedAwaiterGivesItsLinkBack) {
        WarmUpTheDelayedQueue();

        const size_t delayedBefore = Scheduler().GetDelayedTaskCount();
        const size_t linksBefore = Detail::TimerLink::PooledPool().GetLiveCount();
        {
            EventAwaiterWithTimeout<AwaitedEvent> awaiter{kShortTimeout};
            ExpectNoAllocations([&awaiter] { awaiter.await_suspend(std::noop_coroutine()); });
        }
        RunUntilTheTimeoutsAreDue(delayedBefore);
        EXPECT_EQ(Detail::TimerLink::PooledPool().GetLiveCount(), linksBefore);
    }

    TEST(SubscriptionAllocationTest, DISABLED_TheFirstWaiterOfATweenHandleAllocatesNothing) {
        TweenManager tweens;
        const IntrusivePtr<TweenHandle> handle = tweens.Delay(1.0f);
        const int owner = 0;

        ExpectNoAllocations([&] { handle->AddWaiter(&owner, std::noop_coroutine()); });
        handle->RemoveWaiter(&owner);
    }

}  // namespace BECore::Tests
