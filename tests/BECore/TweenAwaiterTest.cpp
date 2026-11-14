#include <BECore/Animation/ScopedTween.h>
#include <BECore/Animation/TweenManager.h>
#include <BECore/Time/Time.h>
#include <TaskSystem/Task.h>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    static_assert(!std::is_copy_constructible_v<TweenAwaiter> && !std::is_move_constructible_v<TweenAwaiter>, "the handle keeps the address of the awaiter while the coroutine waits");

    TEST(TweenAwaiterTest, CompileTimeChecks) {}

    namespace {

        // Frame times and durations are powers of two, so the sums are exact in floating point
        constexpr int kFrameRates[] = {2, 4, 8, 16};
        constexpr float kOneSecond = 1.0f;
        constexpr float kHalfSecond = 0.5f;
        constexpr float kQuarterSecond = 0.25f;
        constexpr float kTo = 100.0f;
        constexpr int kFramesInASecond = 32;
        constexpr int kFramesBeforeTheLast = static_cast<int>(kOneSecond / kQuarterSecond) - 1;  // the tween of a second ends in the Advance after these
        constexpr int kChainLength = 3;
        constexpr int kWaiterCount = 3;
        constexpr auto kRealTimePause = std::chrono::milliseconds(20);

        TweenSettings Settings(float duration) {
            return {.duration = duration};
        }

        /// What the coroutines of a test write down
        struct Log {
            Vector<int> order;  // the ids of the coroutines that continued, in the order they did
            Vector<TweenResult> results;
            float watchedAtResume = -1.0f;
        };

        /// Awaits @p handle, then writes the id and the result into @p log
        Task<void> AwaitHandle(IntrusivePtr<TweenHandle> handle, Log& log, int id, const float* watched = nullptr) {
            const TweenResult result = co_await AwaitTween(std::move(handle));
            log.order.push_back(id);
            log.results.push_back(result);
            if (watched != nullptr) {
                log.watchedAtResume = *watched;
            }
        }

        /// The coroutine frame holds @p guard: a use count of 1 on the test side means the frame is gone
        Task<void> AwaitHandleWithGuard([[maybe_unused]] SharedPtr<int> guard, IntrusivePtr<TweenHandle> handle, Log& log, int id) {
            const TweenResult result = co_await AwaitTween(std::move(handle));
            log.order.push_back(id);
            log.results.push_back(result);
        }

        /// Starts a tween with no setter and awaits it, @p kChainLength times
        Task<void> Chain(TweenManager& tweens, Log& log) {
            for (int link = 0; link < kChainLength; ++link) {
                co_await AwaitTween(tweens.Delay(kQuarterSecond));
                log.order.push_back(link);
            }
        }

        /// Awaits @p handle, then cancels @p victim
        Task<void> AwaitThenCancel(IntrusivePtr<TweenHandle> handle, IntrusivePtr<TweenHandle> victim, Log& log, int id) {
            co_await AwaitTween(std::move(handle));
            log.order.push_back(id);
            victim->Cancel();
        }

        /// Awaits @p handle, then drops the task @p other (its frame is destroyed while it waits on the same handle)
        Task<void> AwaitThenDrop(IntrusivePtr<TweenHandle> handle, Task<void>& other, Log& log, int id) {
            co_await AwaitTween(std::move(handle));
            log.order.push_back(id);
            other = Task<void>{};
        }

        /// Awaits @p handle, then clears the manager
        Task<void> AwaitThenClear(IntrusivePtr<TweenHandle> handle, TweenManager& tweens, Log& log, int id) {
            co_await AwaitTween(std::move(handle));
            log.order.push_back(id);
            tweens.Clear();
        }

        Task<void> WaitHalfSecond(TweenManager& tweens, Log& log, int id) {
            const TweenResult result = co_await tweens.Wait(kHalfSecond);
            log.order.push_back(id);
            log.results.push_back(result);
        }

        /// `co_await Wait(a); write 1; co_await Wait(b); write 2`
        Task<void> Sequence(TweenManager& tweens, Vector<int>& values) {
            co_await tweens.Wait(kQuarterSecond);
            values.push_back(1);
            co_await tweens.Wait(kHalfSecond);
            values.push_back(2);
        }

        IntrusivePtr<TweenHandle> StartSilent(TweenManager& tweens, float duration) {
            return tweens.Start<float>(0.0f, kTo, Settings(duration), {});
        }

    }  // namespace

    TEST(TweenAwaiterTest, TheCoroutineResumesInExactlyTheAdvanceThatCompletesTheTween) {
        TweenManager tweens;
        Log log;
        Task<void> task = AwaitHandle(StartSilent(tweens, kOneSecond), log, 1);

        task.Resume();
        EXPECT_TRUE(log.order.empty());  // suspended on the tween
        for (int frame = 0; frame < kFramesBeforeTheLast; ++frame) {
            tweens.Advance(kQuarterSecond);
            EXPECT_TRUE(log.order.empty()) << frame;
        }
        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(log.order, (Vector<int>{1}));
        ASSERT_EQ(log.results.size(), 1u);
        EXPECT_EQ(log.results[0], TweenResult::Completed);
        EXPECT_TRUE(task.IsDone());
    }

    TEST(TweenAwaiterTest, OneSecondOfAdvanceEndsAtTheTargetAndResumesOnceAtEveryFrameRate) {
        for (const int framesPerSecond : kFrameRates) {
            TweenManager tweens;
            Log log;
            float value = 0.0f;
            const auto handle = tweens.Start<float>(0.0f, kTo, Settings(kOneSecond), [&value](const float& next) { value = next; });
            Task<void> task = AwaitHandle(handle, log, 1, &value);
            task.Resume();
            const float frameTime = kOneSecond / static_cast<float>(framesPerSecond);

            for (int frame = 0; frame < framesPerSecond - 1; ++frame) {
                tweens.Advance(frameTime);
            }
            EXPECT_TRUE(log.order.empty()) << framesPerSecond;
            tweens.Advance(frameTime);

            EXPECT_EQ(log.order, (Vector<int>{1})) << framesPerSecond;
            EXPECT_EQ(log.watchedAtResume, kTo) << framesPerSecond;  // the setter ran before the coroutine continued
        }
    }

    TEST(TweenAwaiterTest, TheWaitDependsOnTheAdvancedTimeAndNotOnTheClock) {
        TweenManager tweens;
        Log log;
        Task<void> task = AwaitHandle(StartSilent(tweens, kOneSecond), log, 1);
        task.Resume();

        std::this_thread::sleep_for(kRealTimePause);  // real time passes, game time does not
        EXPECT_TRUE(log.order.empty());

        tweens.Advance(kOneSecond);  // one second of game time in an instant
        EXPECT_EQ(log.order, (Vector<int>{1}));
    }

    TEST(TweenAwaiterTest, DrivenByTheFrameTimeOfATimeObject) {
        TweenManager tweens;
        Time time;
        Log log;
        Task<void> task = AwaitHandle(StartSilent(tweens, kOneSecond), log, 1);
        task.Resume();

        for (int frame = 0; frame < kFramesInASecond - 1; ++frame) {
            time.Advance(1.0 / kFramesInASecond);
            tweens.Advance(time.GetDeltaTime());
        }
        EXPECT_TRUE(log.order.empty());
        time.Advance(1.0 / kFramesInASecond);
        tweens.Advance(time.GetDeltaTime());

        EXPECT_EQ(log.order, (Vector<int>{1}));
        EXPECT_EQ(log.results[0], TweenResult::Completed);
    }

    TEST(TweenAwaiterTest, CancelResumesTheWaiterWithCancelled) {
        TweenManager tweens;
        Log log;
        Vector<int> events;
        const auto handle = tweens.Start<float>(0.0f, kTo, Settings(kOneSecond), {}, [&events] { events.push_back(-1); }, [&events] { events.push_back(-2); });
        Task<void> task = AwaitHandle(handle, log, 1);
        task.Resume();

        handle->Cancel();

        EXPECT_EQ(log.order, (Vector<int>{1}));
        EXPECT_EQ(log.results[0], TweenResult::Cancelled);
        EXPECT_EQ(events, (Vector<int>{-2}));  // the cancel callback ran, the completion did not
        EXPECT_TRUE(task.IsDone());
    }

    TEST(TweenAwaiterTest, TheWaiterContinuesAfterTheCompletionCallbackOfItsTween) {
        TweenManager tweens;
        Log log;
        const auto handle = tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond), {}, [&log] { log.order.push_back(0); });
        Task<void> task = AwaitHandle(handle, log, 1);
        task.Resume();

        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(log.order, (Vector<int>{0, 1}));
    }

    TEST(TweenAwaiterTest, CancelAllResumesEveryWaiterWithCancelledInStartOrder) {
        TweenManager tweens;
        Log log;
        Vector<Task<void>> tasks;
        for (int id = 0; id < kWaiterCount; ++id) {
            tasks.push_back(AwaitHandle(StartSilent(tweens, kOneSecond), log, id));
            tasks.back().Resume();
        }

        tweens.CancelAll();

        EXPECT_EQ(log.order, (Vector<int>{0, 1, 2}));
        for (const TweenResult result : log.results) {
            EXPECT_EQ(result, TweenResult::Cancelled);
        }
    }

    TEST(TweenAwaiterTest, AScopedTweenDestructorResumesTheWaiterWithCancelled) {
        TweenManager tweens;
        Log log;
        Task<void> task;
        {
            ScopedTween scoped(StartSilent(tweens, kOneSecond));
            task = AwaitHandle(scoped.Get(), log, 1);
            task.Resume();
            EXPECT_TRUE(log.order.empty());
        }  // the destructor cancels the tween and the coroutine continues in there

        EXPECT_EQ(log.order, (Vector<int>{1}));
        EXPECT_EQ(log.results[0], TweenResult::Cancelled);
    }

    TEST(TweenAwaiterTest, AFinishedHandleDoesNotSuspend) {
        TweenManager tweens;
        const auto completed = StartSilent(tweens, kQuarterSecond);
        tweens.Advance(kQuarterSecond);
        const auto cancelled = StartSilent(tweens, kOneSecond);
        cancelled->Cancel();

        const TweenAwaiter afterCompletion = AwaitTween(completed);
        const TweenAwaiter afterCancel = AwaitTween(cancelled);
        EXPECT_TRUE(afterCompletion.await_ready());
        EXPECT_EQ(afterCompletion.await_resume(), TweenResult::Completed);
        EXPECT_TRUE(afterCancel.await_ready());
        EXPECT_EQ(afterCancel.await_resume(), TweenResult::Cancelled);

        Log log;
        Task<void> first = AwaitHandle(completed, log, 1);
        Task<void> second = AwaitHandle(cancelled, log, 2);
        first.Resume();
        second.Resume();  // both run to the end at once, no Advance
        EXPECT_EQ(log.order, (Vector<int>{1, 2}));
        EXPECT_EQ(log.results[0], TweenResult::Completed);
        EXPECT_EQ(log.results[1], TweenResult::Cancelled);
    }

    TEST(TweenAwaiterTest, ARunningHandleSuspendsAndAnEmptyOneReadsCancelled) {
        TweenManager tweens;
        const TweenAwaiter running = AwaitTween(StartSilent(tweens, kOneSecond));
        EXPECT_FALSE(running.await_ready());

        const TweenAwaiter nothing = AwaitTween({});
        EXPECT_TRUE(nothing.await_ready());
        EXPECT_EQ(nothing.await_resume(), TweenResult::Cancelled);
    }

    TEST(TweenAwaiterTest, AHandleWhoseManagerWasClearedReadsCancelledAndANewAwaitReturnsAtOnce) {
        TweenManager tweens;
        Log log;
        const auto handle = StartSilent(tweens, kOneSecond);
        tweens.Clear();

        Task<void> task = AwaitHandle(handle, log, 1);
        task.Resume();

        EXPECT_EQ(log.order, (Vector<int>{1}));
        EXPECT_EQ(log.results[0], TweenResult::Cancelled);
    }

    TEST(TweenAwaiterTest, ClearNeverResumesAWaiterAndTheFrameIsFreedByDroppingTheTask) {
        TweenManager tweens;
        Log log;
        auto guard = MakeShared<int>(0);
        {
            Task<void> task = AwaitHandleWithGuard(guard, StartSilent(tweens, kOneSecond), log, 1);
            task.Resume();
            ASSERT_EQ(guard.use_count(), 2);  // the frame holds one

            tweens.Clear();
            tweens.Advance(kOneSecond);

            EXPECT_TRUE(log.order.empty());
            EXPECT_FALSE(task.IsDone());
        }  // the Task is dropped: the suspended frame is destroyed, its awaiter with it

        EXPECT_EQ(guard.use_count(), 1);
        EXPECT_TRUE(log.order.empty());
    }

    TEST(TweenAwaiterTest, TheDestructorOfTheManagerNeverResumesAWaiter) {
        Log log;
        auto guard = MakeShared<int>(0);
        IntrusivePtr<TweenHandle> handle;
        Task<void> task;
        {
            TweenManager tweens;
            handle = StartSilent(tweens, kOneSecond);
            task = AwaitHandleWithGuard(guard, handle, log, 1);
            task.Resume();
        }

        EXPECT_TRUE(handle->IsCancelled());
        EXPECT_TRUE(log.order.empty());
        EXPECT_FALSE(task.IsDone());

        task = Task<void>{};  // the frame goes, the awaiter finds no manager and nothing to unregister from
        EXPECT_EQ(guard.use_count(), 1);
    }

    TEST(TweenAwaiterTest, ATaskDroppedWhileItWaitsIsNeverResumed) {
        TweenManager tweens;
        Log log;
        auto guard = MakeShared<int>(0);
        const auto handle = StartSilent(tweens, kOneSecond);
        {
            Task<void> task = AwaitHandleWithGuard(guard, handle, log, 1);
            task.Resume();
            ASSERT_EQ(guard.use_count(), 2);
        }  // the awaiter unregisters in its destructor
        EXPECT_EQ(guard.use_count(), 1);

        tweens.Advance(kOneSecond);  // would resume a destroyed frame if the waiter were still registered

        EXPECT_TRUE(handle->IsFinished());
        EXPECT_TRUE(log.order.empty());
    }

    TEST(TweenAwaiterTest, ASiblingWaiterDestroyedByAnEarlierOneIsNotResumed) {
        TweenManager tweens;
        Log log;
        const auto handle = StartSilent(tweens, kQuarterSecond);
        Task<void> second;
        Task<void> first = AwaitThenDrop(handle, second, log, 1);
        second = AwaitHandle(handle, log, 2);
        first.Resume();
        second.Resume();

        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(log.order, (Vector<int>{1}));  // the second frame was destroyed by the first before its turn
        EXPECT_FALSE(second.IsValid());
    }

    TEST(TweenAwaiterTest, AClearFromAContinuationDropsTheWaitersThatCameAfter) {
        TweenManager tweens;
        Log log;
        const auto handle = StartSilent(tweens, kQuarterSecond);
        Task<void> first = AwaitThenClear(handle, tweens, log, 1);
        Task<void> second = AwaitHandle(handle, log, 2);
        first.Resume();
        second.Resume();

        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(log.order, (Vector<int>{1}));
        EXPECT_FALSE(second.IsDone());
    }

    TEST(TweenAwaiterTest, SeveralCoroutinesOnOneHandleResumeInTheOrderTheyAwaited) {
        TweenManager tweens;
        Log log;
        const auto handle = StartSilent(tweens, kQuarterSecond);
        Vector<Task<void>> tasks;
        for (int id = kWaiterCount; id > 0; --id) {
            tasks.push_back(AwaitHandle(handle, log, id));
            tasks.back().Resume();
        }

        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(log.order, (Vector<int>{3, 2, 1}));
    }

    TEST(TweenAwaiterTest, AChainOfThreeTakesOneAdvancePerLinkAndDropsTheLeftOverTime) {
        TweenManager tweens;
        Log log;
        Task<void> task = Chain(tweens, log);
        task.Resume();  // starts the first link

        tweens.Advance(kOneSecond);  // four times the link: the left over is dropped, and the second link starts inside
        EXPECT_EQ(log.order, (Vector<int>{0}));
        tweens.Advance(kOneSecond);
        EXPECT_EQ(log.order, (Vector<int>{0, 1}));
        EXPECT_FALSE(task.IsDone());
        tweens.Advance(kOneSecond);
        EXPECT_EQ(log.order, (Vector<int>{0, 1, 2}));
        EXPECT_TRUE(task.IsDone());
    }

    TEST(TweenAwaiterTest, ACoroutineMayCancelAnotherTweenFromItsContinuation) {
        TweenManager tweens;
        Log log;
        const auto quick = StartSilent(tweens, kQuarterSecond);
        const auto slow = StartSilent(tweens, kOneSecond);
        Task<void> canceller = AwaitThenCancel(quick, slow, log, 1);
        Task<void> victim = AwaitHandle(slow, log, 2);
        canceller.Resume();
        victim.Resume();

        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(log.order, (Vector<int>{1, 2}));  // the cancelled tween resumed its own waiter inside the cancel
        ASSERT_EQ(log.results.size(), 1u);
        EXPECT_EQ(log.results[0], TweenResult::Cancelled);
        EXPECT_TRUE(slow->IsCancelled());
        EXPECT_EQ(tweens.GetActiveCount(), 0u);
    }

    TEST(TweenAwaiterTest, WaitResumesAfterThatMuchAdvancedTime) {
        TweenManager tweens;
        Log log;
        Task<void> task = WaitHalfSecond(tweens, log, 1);
        task.Resume();

        tweens.Advance(kQuarterSecond);
        EXPECT_TRUE(log.order.empty());
        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(log.order, (Vector<int>{1}));
        EXPECT_EQ(log.results[0], TweenResult::Completed);
    }

    TEST(TweenAwaiterTest, DelayRunsItsCallbacksLikeATweenWithNoSetter) {
        TweenManager tweens;
        int completed = 0;
        int cancelled = 0;
        const auto finishing = tweens.Delay(kQuarterSecond, [&] { ++completed; }, [&] { ++cancelled; });
        const auto cancelledOne = tweens.Delay(kOneSecond, [&] { ++completed; }, [&] { ++cancelled; });

        tweens.Advance(kQuarterSecond);
        EXPECT_EQ(finishing->GetState(), TweenState::Completed);
        EXPECT_EQ(completed, 1);

        cancelledOne->Cancel();
        EXPECT_EQ(cancelled, 1);
        EXPECT_EQ(completed, 1);
    }

    TEST(TweenAwaiterTest, ASequenceOfWaitsWritesItsValuesInOrder) {
        TweenManager tweens;
        Vector<int> values;
        Task<void> task = Sequence(tweens, values);
        task.Resume();
        EXPECT_TRUE(values.empty());

        tweens.Advance(kQuarterSecond);
        EXPECT_EQ(values, (Vector<int>{1}));
        tweens.Advance(kQuarterSecond);
        EXPECT_EQ(values, (Vector<int>{1}));
        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(values, (Vector<int>{1, 2}));
        EXPECT_TRUE(task.IsDone());
    }

    TEST(TweenAwaiterTest, ANotFiniteWaitCompletesAtOnce) {
        TweenManager tweens;
        Log log;
        Task<void> task = AwaitHandle(tweens.Delay(std::numeric_limits<float>::quiet_NaN()), log, 1);
        task.Resume();

        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(log.order, (Vector<int>{1}));
        EXPECT_EQ(log.results[0], TweenResult::Completed);
    }

}  // namespace BECore::Tests
