#include <BECore/Animation/ScopedTween.h>
#include <BECore/Animation/TweenManager.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/Time/Time.h>
#include <Common/DestroyTally.h>
#include <Common/PoolCounters.h>
#include <Common/Require.h>
#include <TaskSystem/Task.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Frame times and durations are powers of two, so the sums are exact in floating point
        constexpr int kFrameRates[] = {2, 4, 8, 16};
        constexpr float kOneSecond = 1.0f;
        constexpr float kHalfSecond = 0.5f;
        constexpr float kQuarterSecond = 0.25f;
        constexpr float kTo = 100.0f;
        constexpr float kHalfTo = 50.0f;     // a linear tween of kTo at half of its time
        constexpr float kQuarterTo = 25.0f;  // ... at a quarter of its time
        constexpr float kSixteenthSecond = 0.0625f;
        constexpr float kSixtyFourthSecond = 0.015625f;
        constexpr float kHugeFrame = 1000.0f;
        constexpr int kLaterTweenCount = 2;
        constexpr int kFramesInASecond = 32;
        constexpr int kWarmTweenCount = 8;
        constexpr float kThreeEighthsSecond = 0.375f;  // does not divide a turn of one second: every turn ends inside a frame
        constexpr int kLoopCount = 8;
        constexpr int kTurnLengths = 4;       // loop k of a test turns in 1 + k % this quarters of a second
        constexpr int kNeighbourCount = 160;  // cancelled together they pass the tombstones a walk tolerates before it compacts
        constexpr int kNeighboursPerLoop = kNeighbourCount / kLoopCount;
        constexpr int kManyFrames = 64;

        TweenSettings Settings(float duration, float delay = 0.0f, EaseType ease = EaseType::Linear) {
            return {duration, delay, ease};
        }

        /// A float tween from 0 to kTo that records every value it was given
        struct Recorder {
            Vector<float> values;
            int completed = 0;
            int cancelled = 0;

            IntrusivePtr<TweenHandle> Start(TweenManager& tweens, TweenSettings settings, float to = kTo) {
                return tweens.Start<float>(0.0f, to, settings, [this](const float& value) { values.push_back(value); }, [this] { ++completed; }, [this] { ++cancelled; });
            }

            IntrusivePtr<TweenHandle> Loop(TweenManager& tweens, TweenSettings settings) {
                return tweens.Loop<float>(0.0f, kTo, settings, [this](const float& value) { values.push_back(value); }, [this] { ++cancelled; });
            }
        };

        Task<void> AwaitResult(IntrusivePtr<TweenHandle> handle, Vector<TweenResult>& results) {
            results.push_back(co_await AwaitTween(std::move(handle)));
        }

        /// The three callables of a tween, each holding a move-only token that counts its end
        struct Tallies {
            int value = 0;
            int complete = 0;
            int cancel = 0;
        };

        IntrusivePtr<TweenHandle> StartTracked(TweenManager& tweens, Tallies& tallies) {
            return tweens.Start<float>(
                0.0f, kTo, Settings(kOneSecond), [token = DestroyTally(tallies.value)](const float&) {}, [token = DestroyTally(tallies.complete)] {}, [token = DestroyTally(tallies.cancel)] {});
        }

        /// Allocations of the calling thread while @p body runs (all zero when the allocation counting is off)
        template <typename TBody>
        uint64_t AllocationsOf(TBody&& body) {
            const AllocTracker::ThreadCounters before = AllocTracker::GetThreadCounters();
            body();
            return AllocTracker::GetThreadCounters().count - before.count;
        }

    }  // namespace

    TEST(TweenManagerTest, FloatEndsExactlyOnTheTargetAtEveryFrameRate) {
        for (const int framesPerSecond : kFrameRates) {
            TweenManager tweens;
            Recorder recorder;
            const auto handle = recorder.Start(tweens, Settings(kOneSecond));
            const float frameTime = kOneSecond / static_cast<float>(framesPerSecond);

            for (int frame = 0; frame < framesPerSecond; ++frame) {
                tweens.Advance(frameTime);
            }

            ASSERT_FALSE(recorder.values.empty());
            EXPECT_EQ(Back(recorder.values), kTo) << framesPerSecond;
            EXPECT_EQ(recorder.values.size(), static_cast<size_t>(framesPerSecond));
            EXPECT_EQ(recorder.completed, 1);
            EXPECT_EQ(recorder.cancelled, 0);
            EXPECT_EQ(handle->GetState(), TweenState::Completed);
            EXPECT_EQ(tweens.GetActiveCount(), 0u);
        }
    }

    TEST(TweenManagerTest, LinearTweenAtHalfTimeIsTheHalfValue) {
        TweenManager tweens;
        Recorder recorder;
        const auto handle = recorder.Start(tweens, Settings(kOneSecond));

        tweens.Advance(kHalfSecond);

        ASSERT_EQ(recorder.values.size(), 1u);
        EXPECT_EQ(recorder.values[0], kHalfTo);
        EXPECT_EQ(handle->GetState(), TweenState::Running);
        EXPECT_EQ(handle->GetProgress(), 0.5f);
        EXPECT_EQ(recorder.completed, 0);
    }

    TEST(TweenManagerTest, EasedTweenFollowsEase) {
        for (const EaseType ease : {EaseType::QuadIn, EaseType::CubicOut, EaseType::SineInOut, EaseType::BackOut, EaseType::BounceOut}) {
            TweenManager tweens;
            Recorder recorder;
            const auto handle = recorder.Start(tweens, Settings(kOneSecond, 0.0f, ease));

            tweens.Advance(kQuarterSecond);
            tweens.Advance(kQuarterSecond);
            tweens.Advance(kHalfSecond);

            ASSERT_EQ(recorder.values.size(), 3u);
            EXPECT_EQ(recorder.values[0], Lerp(0.0f, kTo, Ease(ease, 0.25f)));
            EXPECT_EQ(recorder.values[1], Lerp(0.0f, kTo, Ease(ease, 0.5f)));
            EXPECT_EQ(recorder.values[2], kTo);
        }
    }

    TEST(TweenManagerTest, Vec2TweenMovesBothComponents) {
        TweenManager tweens;
        Vec2 position{1.0f, 2.0f};
        tweens.Start<Vec2>(Vec2{0.0f, 10.0f}, Vec2{4.0f, -10.0f}, Settings(kOneSecond), [&position](const Vec2& value) { position = value; });

        tweens.Advance(kHalfSecond);
        EXPECT_EQ(position, Vec2(2.0f, 0.0f));

        tweens.Advance(kHalfSecond);
        EXPECT_EQ(position, Vec2(4.0f, -10.0f));
    }

    TEST(TweenManagerTest, ColorTweenEndsOnTheTarget) {
        TweenManager tweens;
        Color color;
        tweens.Start<Color>(Color{0, 100, 255, 10}, Color{255, 100, 0, 10}, Settings(kOneSecond), [&color](const Color& value) { color = value; });

        tweens.Advance(kHalfSecond);
        EXPECT_EQ(color.r, 128);
        EXPECT_EQ(color.g, 100);
        EXPECT_EQ(color.b, 128);

        tweens.Advance(kHalfSecond);
        EXPECT_EQ(color.r, 255);
        EXPECT_EQ(color.b, 0);
        EXPECT_EQ(color.a, 10);
    }

    TEST(TweenManagerTest, SetterIsNotCalledDuringTheDelay) {
        TweenManager tweens;
        Recorder recorder;
        const auto handle = recorder.Start(tweens, Settings(kOneSecond, kHalfSecond));

        tweens.Advance(kQuarterSecond);
        tweens.Advance(kQuarterSecond - kSixteenthSecond);
        EXPECT_TRUE(recorder.values.empty());
        EXPECT_EQ(handle->GetState(), TweenState::Waiting);
        EXPECT_EQ(handle->GetProgress(), 0.0f);

        tweens.Advance(kSixteenthSecond);  // exactly at the end of the delay: progress 0, the value is `from`
        ASSERT_EQ(recorder.values.size(), 1u);
        EXPECT_EQ(recorder.values[0], 0.0f);
        EXPECT_EQ(handle->GetState(), TweenState::Running);
    }

    TEST(TweenManagerTest, TheStepThatPassesTheDelayAppliesTheTimeLeft) {
        TweenManager tweens;
        Recorder recorder;
        recorder.Start(tweens, Settings(kOneSecond, kQuarterSecond));

        tweens.Advance(kHalfSecond + kQuarterSecond);  // 0.25 s of delay, then 0.5 s of the tween

        ASSERT_EQ(recorder.values.size(), 1u);
        EXPECT_EQ(recorder.values[0], kHalfTo);
    }

    TEST(TweenManagerTest, ZeroAndNegativeDurationCompleteOnTheFirstAdvance) {
        for (const float duration : {0.0f, -1.0f}) {
            TweenManager tweens;
            Recorder recorder;
            recorder.Start(tweens, Settings(duration));
            EXPECT_TRUE(recorder.values.empty());

            tweens.Advance(kSixtyFourthSecond);

            ASSERT_EQ(recorder.values.size(), 1u);
            EXPECT_EQ(recorder.values[0], kTo);
            EXPECT_EQ(recorder.completed, 1);
        }
    }

    TEST(TweenManagerTest, ZeroDurationWithADelayWaitsForTheDelay) {
        TweenManager tweens;
        Recorder recorder;
        recorder.Start(tweens, Settings(0.0f, kHalfSecond));

        tweens.Advance(kQuarterSecond);
        EXPECT_TRUE(recorder.values.empty());

        tweens.Advance(kQuarterSecond);
        ASSERT_EQ(recorder.values.size(), 1u);
        EXPECT_EQ(recorder.values[0], kTo);
        EXPECT_EQ(recorder.completed, 1);
    }

    TEST(TweenManagerTest, ZeroNegativeAndNaNFramesAdvanceNothing) {
        TweenManager tweens;
        Recorder recorder;
        const auto handle = recorder.Start(tweens, Settings(kOneSecond));

        tweens.Advance(0.0f);
        tweens.Advance(-1.0f);
        tweens.Advance(std::numeric_limits<float>::quiet_NaN());

        EXPECT_TRUE(recorder.values.empty());
        EXPECT_EQ(handle->GetState(), TweenState::Waiting);

        tweens.Advance(kHalfSecond);
        EXPECT_EQ(Back(recorder.values), kHalfTo);
    }

    TEST(TweenManagerTest, CompletionRunsOnceAfterTheLastValueAndNothingMoreAfterIt) {
        TweenManager tweens;
        Vector<int> log;  // 0 is a value, 1 the completion
        tweens.Start<float>(0.0f, kTo, Settings(kHalfSecond), [&log](const float&) { log.push_back(0); }, [&log] { log.push_back(1); });

        tweens.Advance(kQuarterSecond);
        tweens.Advance(kHalfSecond);  // more than is left
        tweens.Advance(kOneSecond);
        tweens.Advance(kOneSecond);

        const Vector<int> expected{0, 0, 1};
        EXPECT_EQ(log, expected);
    }

    TEST(TweenManagerTest, CancelRunsTheCallbackOnceAndStopsTheSetter) {
        TweenManager tweens;
        Recorder recorder;
        const auto handle = recorder.Start(tweens, Settings(kOneSecond));
        tweens.Advance(kQuarterSecond);

        handle->Cancel();
        handle->Cancel();
        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(recorder.cancelled, 1);
        EXPECT_EQ(recorder.completed, 0);
        EXPECT_EQ(recorder.values.size(), 1u);  // the value is not set by the cancel
        EXPECT_TRUE(handle->IsCancelled());
        EXPECT_TRUE(handle->IsFinished());
        EXPECT_EQ(handle->GetProgress(), 0.25f);
        EXPECT_EQ(tweens.GetActiveCount(), 0u);
    }

    TEST(TweenManagerTest, CancelOfAWaitingTweenRunsTheCallback) {
        TweenManager tweens;
        Recorder recorder;
        const auto handle = recorder.Start(tweens, Settings(kOneSecond, kOneSecond));

        handle->Cancel();

        EXPECT_EQ(recorder.cancelled, 1);
        EXPECT_TRUE(recorder.values.empty());
    }

    TEST(TweenManagerTest, CancelAfterCompletionDoesNothing) {
        TweenManager tweens;
        Recorder recorder;
        const auto handle = recorder.Start(tweens, Settings(kHalfSecond));
        tweens.Advance(kOneSecond);

        handle->Cancel();

        EXPECT_EQ(recorder.cancelled, 0);
        EXPECT_EQ(recorder.completed, 1);
        EXPECT_EQ(handle->GetState(), TweenState::Completed);
        EXPECT_EQ(handle->GetProgress(), 1.0f);
    }

    TEST(TweenManagerTest, ADroppedHandleDoesNotStopTheTween) {
        TweenManager tweens;
        Recorder recorder;
        recorder.Start(tweens, Settings(kHalfSecond));

        tweens.Advance(kOneSecond);

        EXPECT_EQ(recorder.completed, 1);
    }

    TEST(TweenManagerTest, ASetterMayCancelItsOwnTween) {
        TweenManager tweens;
        IntrusivePtr<TweenHandle> handle;
        int setterCalls = 0;
        int cancelled = 0;
        int completed = 0;
        handle = tweens.Start<float>(
            0.0f, kTo, Settings(kHalfSecond),
            [&](const float&) {
                ++setterCalls;
                handle->Cancel();
            },
            [&] { ++completed; }, [&] { ++cancelled; });

        tweens.Advance(kQuarterSecond);
        tweens.Advance(kOneSecond);

        EXPECT_EQ(setterCalls, 1);
        EXPECT_EQ(cancelled, 1);
        EXPECT_EQ(completed, 0);
    }

    TEST(TweenManagerTest, ASetterThatCancelsOnItsLastCallGetsNoCompletion) {
        TweenManager tweens;
        IntrusivePtr<TweenHandle> handle;
        int cancelled = 0;
        int completed = 0;
        handle = tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond), [&](const float&) { handle->Cancel(); }, [&] { ++completed; }, [&] { ++cancelled; });

        tweens.Advance(kOneSecond);

        EXPECT_EQ(cancelled, 1);
        EXPECT_EQ(completed, 0);
    }

    TEST(TweenManagerTest, ACompletionCallbackMayCancelAnotherTween) {
        TweenManager tweens;
        Recorder first;
        Recorder second;
        tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond), [&](const float& value) { first.values.push_back(value); }, [&] { ++first.completed; });
        const auto secondHandle = second.Start(tweens, Settings(kOneSecond));
        tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond), [](const float&) {}, [&] { secondHandle->Cancel(); });

        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(first.completed, 1);
        EXPECT_EQ(second.cancelled, 1);
        EXPECT_EQ(second.values.size(), 1u);  // it ran before the third one cancelled it, nothing after
        tweens.Advance(kQuarterSecond);
        EXPECT_EQ(second.values.size(), 1u);
        EXPECT_EQ(second.cancelled, 1);
    }

    TEST(TweenManagerTest, ACallbackThatCancelsTheNextTweenInTheListSkipsItsSetter) {
        TweenManager tweens;
        Recorder next;
        IntrusivePtr<TweenHandle> nextHandle;
        tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond), [](const float&) {}, [&] { nextHandle->Cancel(); });
        nextHandle = next.Start(tweens, Settings(kQuarterSecond));

        tweens.Advance(kQuarterSecond);

        EXPECT_TRUE(next.values.empty());
        EXPECT_EQ(next.cancelled, 1);
        EXPECT_EQ(next.completed, 0);
    }

    TEST(TweenManagerTest, ATweenStartedInACompletionRunsFromTheNextAdvance) {
        TweenManager tweens;
        Recorder chained;
        int advancesBeforeFirstValue = 0;
        tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond), [](const float&) {}, [&] { chained.Start(tweens, Settings(kQuarterSecond)); });

        tweens.Advance(kOneSecond);  // far more than both tweens last
        EXPECT_TRUE(chained.values.empty());
        EXPECT_EQ(tweens.GetActiveCount(), 1u);

        while (chained.values.empty() && advancesBeforeFirstValue < 4) {
            tweens.Advance(kQuarterSecond);
            ++advancesBeforeFirstValue;
        }

        EXPECT_EQ(advancesBeforeFirstValue, 1);
        ASSERT_EQ(chained.values.size(), 1u);
        EXPECT_EQ(chained.values[0], kTo);
        EXPECT_EQ(chained.completed, 1);
    }

    TEST(TweenManagerTest, ATweenStartedInACancelCallbackFromAdvanceWaitsToo) {
        TweenManager tweens;
        Recorder chained;
        IntrusivePtr<TweenHandle> victim;
        tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond), [](const float&) {}, [&] { victim->Cancel(); });
        victim = tweens.Start<float>(0.0f, kTo, Settings(kOneSecond), [](const float&) {}, {}, [&] { chained.Start(tweens, Settings(kQuarterSecond)); });

        tweens.Advance(kQuarterSecond);
        EXPECT_TRUE(chained.values.empty());

        tweens.Advance(kQuarterSecond);
        EXPECT_EQ(chained.completed, 1);
    }

    TEST(TweenManagerTest, TweensCompleteInTheOrderTheyWereStarted) {
        TweenManager tweens;
        Vector<int> order;
        for (int id = 0; id < 3; ++id) {
            tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond), [](const float&) {}, [&order, id] { order.push_back(id); });
        }

        tweens.Advance(kQuarterSecond);

        const Vector<int> expected{0, 1, 2};
        EXPECT_EQ(order, expected);
    }

    TEST(TweenManagerTest, StartOrderBeatsFinishOrderWithinOneAdvanceAndTimeBeatsStartOrderAcrossThem) {
        TweenManager tweens;
        Vector<char> order;
        // A starts first but waits longer: B finishes in an earlier Advance
        tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond, kHalfSecond), [](const float&) {}, [&] { order.push_back('A'); });
        tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond, 0.0f), [](const float&) {}, [&] { order.push_back('B'); });

        tweens.Advance(kQuarterSecond);
        tweens.Advance(kQuarterSecond);
        tweens.Advance(kQuarterSecond);

        const Vector<char> expected{'B', 'A'};
        EXPECT_EQ(order, expected);
    }

    TEST(TweenManagerTest, ATweenStartedInACallbackNeverRunsInTheSameAdvanceEvenWithAHugeFrame) {
        TweenManager tweens;
        int started = 0;
        int values = 0;
        MoveOnlyFunction<void()> chain;
        chain = [&] {
            if (++started < 5) {
                tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond), [&values](const float&) { ++values; }, [&chain] { chain(); });
            }
        };
        tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond), [&values](const float&) { ++values; }, [&chain] { chain(); });

        tweens.Advance(kHugeFrame);

        EXPECT_EQ(started, 1);  // one link per Advance
        EXPECT_EQ(values, 1);
        tweens.Advance(kHugeFrame);
        EXPECT_EQ(started, 2);
        EXPECT_EQ(values, 2);
    }

    TEST(TweenManagerTest, CancelAllCancelsWithCallbacksInStartOrder) {
        TweenManager tweens;
        Vector<int> order;
        Vector<IntrusivePtr<TweenHandle>> handles;
        for (int id = 0; id < 3; ++id) {
            handles.push_back(tweens.Start<float>(0.0f, kTo, Settings(kOneSecond), [](const float&) {}, {}, [&order, id] { order.push_back(id); }));
        }

        tweens.CancelAll();

        const Vector<int> expected{0, 1, 2};
        EXPECT_EQ(order, expected);
        EXPECT_EQ(tweens.GetActiveCount(), 0u);
        for (const auto& handle : handles) {
            EXPECT_TRUE(handle->IsCancelled());
        }
    }

    TEST(TweenManagerTest, ATweenStartedByACancelAllCallbackSurvivesIt) {
        TweenManager tweens;
        Recorder restarted;
        tweens.Start<float>(0.0f, kTo, Settings(kOneSecond), [](const float&) {}, {}, [&] { restarted.Start(tweens, Settings(kQuarterSecond)); });

        tweens.CancelAll();
        EXPECT_EQ(tweens.GetActiveCount(), 1u);

        tweens.Advance(kQuarterSecond);
        EXPECT_EQ(restarted.completed, 1);
    }

    TEST(TweenManagerTest, ClearDropsEverythingWithoutCallbacks) {
        TweenManager tweens;
        Recorder first;
        Recorder second;
        const auto firstHandle = first.Start(tweens, Settings(kOneSecond));
        const auto secondHandle = second.Start(tweens, Settings(kOneSecond, kOneSecond));
        tweens.Advance(kQuarterSecond);

        tweens.Clear();
        tweens.Advance(kOneSecond);

        EXPECT_EQ(tweens.GetActiveCount(), 0u);
        EXPECT_EQ(first.cancelled, 0);
        EXPECT_EQ(second.cancelled, 0);
        EXPECT_EQ(first.completed, 0);
        EXPECT_EQ(first.values.size(), 1u);
        EXPECT_TRUE(firstHandle->IsCancelled());
        EXPECT_TRUE(secondHandle->IsCancelled());
    }

    TEST(TweenManagerTest, ClearFromInsideASetterIsSafe) {
        TweenManager tweens;
        Recorder other;
        int calls = 0;
        const auto before = other.Start(tweens, Settings(kOneSecond));
        const auto clearing = tweens.Start<float>(0.0f, kTo, Settings(kOneSecond), [&](const float&) {
            ++calls;
            tweens.Clear();
        });
        const auto after = other.Start(tweens, Settings(kOneSecond));

        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(calls, 1);
        EXPECT_EQ(tweens.GetActiveCount(), 0u);
        EXPECT_EQ(other.cancelled, 0);
        EXPECT_EQ(other.values.size(), 1u);  // the one before the clearing tween stepped, the one after got no setter call
        EXPECT_TRUE(before->IsCancelled());
        EXPECT_TRUE(clearing->IsCancelled());
        EXPECT_TRUE(after->IsCancelled());
    }

    TEST(TweenManagerTest, TheRunningSetterSurvivesACancelOfItsOwnTween) {
        TweenManager tweens;
        IntrusivePtr<TweenHandle> handle;
        int setterEnded = 0;  // the setter holds a token that counts its end: 0 while it runs, 1 once it was released
        bool aliveInsideAfterCancel = false;
        handle = tweens.Start<float>(0.0f, kTo, Settings(kOneSecond), [&, guard = DestroyTally(setterEnded)](const float&) {
            handle->Cancel();
            aliveInsideAfterCancel = setterEnded == 0;
        });
        ASSERT_EQ(setterEnded, 0);

        tweens.Advance(kQuarterSecond);

        EXPECT_TRUE(aliveInsideAfterCancel);
        EXPECT_EQ(setterEnded, 1);  // released after the setter returned
        EXPECT_TRUE(handle->IsCancelled());
    }

    TEST(TweenManagerTest, TheRunningSetterSurvivesAClearInsideIt) {
        TweenManager tweens;
        int setterEnded = 0;
        bool aliveInsideAfterClear = false;
        tweens.Start<float>(0.0f, kTo, Settings(kOneSecond), [&, guard = DestroyTally(setterEnded)](const float&) {
            tweens.Clear();
            aliveInsideAfterClear = setterEnded == 0;
        });
        ASSERT_EQ(setterEnded, 0);

        tweens.Advance(kQuarterSecond);

        EXPECT_TRUE(aliveInsideAfterClear);
        EXPECT_EQ(setterEnded, 1);
    }

    TEST(TweenManagerTest, CancelAllFromACompletionCallbackCancelsTheRestOfTheWalkAndThePending) {
        TweenManager tweens;
        Vector<int> cancelOrder;
        int laterSetterCalls = 0;
        Recorder started;  // started by the callback before it calls CancelAll: it sits in the pending list
        IntrusivePtr<TweenHandle> startedHandle;
        const auto completing = tweens.Start<float>(
            0.0f, kTo, Settings(kQuarterSecond), [](const float&) {},
            [&] {
                startedHandle = started.Start(tweens, Settings(kQuarterSecond));
                tweens.CancelAll();
            });
        Vector<IntrusivePtr<TweenHandle>> later;
        for (int id = 1; id <= kLaterTweenCount; ++id) {
            later.push_back(tweens.Start<float>(0.0f, kTo, Settings(kOneSecond), [&](const float&) { ++laterSetterCalls; }, {}, [&cancelOrder, id] { cancelOrder.push_back(id); }));
        }

        tweens.Advance(kQuarterSecond);

        const Vector<int> expected{1, 2};
        EXPECT_EQ(cancelOrder, expected);  // in start order
        EXPECT_EQ(laterSetterCalls, 0);
        EXPECT_EQ(completing->GetState(), TweenState::Completed);  // it finished before CancelAll, which leaves it alone
        EXPECT_TRUE(later[0]->IsCancelled());
        EXPECT_TRUE(later[1]->IsCancelled());
        ASSERT_TRUE(startedHandle);
        EXPECT_TRUE(startedHandle->IsCancelled());
        EXPECT_EQ(started.cancelled, 1);
        EXPECT_TRUE(started.values.empty());
        EXPECT_EQ(tweens.GetActiveCount(), 0u);

        tweens.Advance(kOneSecond);  // nothing is left to run
        EXPECT_EQ(laterSetterCalls, 0);
        EXPECT_TRUE(started.values.empty());
    }

    TEST(TweenManagerTest, AdvanceFromInsideACallbackIsIgnored) {
        TweenManager tweens;
        Recorder next;
        tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond), [](const float&) {}, [&] { tweens.Advance(kOneSecond); });
        next.Start(tweens, Settings(kOneSecond));

        tweens.Advance(kQuarterSecond);

        ASSERT_EQ(next.values.size(), 1u);  // one step of the outer Advance; the inner one would have finished it
        EXPECT_EQ(next.values[0], kQuarterTo);
        EXPECT_EQ(next.completed, 0);
    }

    TEST(TweenManagerTest, ADestroyedManagerCancelsItsHandlesWithoutCallbacks) {
        Recorder recorder;
        IntrusivePtr<TweenHandle> handle;
        {
            TweenManager tweens;
            handle = recorder.Start(tweens, Settings(kOneSecond));
            tweens.Advance(kQuarterSecond);
        }

        EXPECT_TRUE(handle->IsCancelled());
        handle->Cancel();  // must not touch the dead manager

        EXPECT_EQ(recorder.cancelled, 0);
        EXPECT_EQ(recorder.values.size(), 1u);
    }

    TEST(TweenManagerTest, ScopedTweenCancelsWhenItGoesOutOfScope) {
        TweenManager tweens;
        Recorder recorder;
        {
            ScopedTween scoped(recorder.Start(tweens, Settings(kOneSecond)));
            tweens.Advance(kQuarterSecond);
            EXPECT_EQ(recorder.cancelled, 0);
        }

        EXPECT_EQ(recorder.cancelled, 1);
        tweens.Advance(kQuarterSecond);
        EXPECT_EQ(recorder.values.size(), 1u);
    }

    TEST(TweenManagerTest, ScopedTweenResetCancelsTheOldOneAndKeepsTheNewOne) {
        TweenManager tweens;
        Recorder first;
        Recorder second;
        ScopedTween scoped;
        scoped.Reset(first.Start(tweens, Settings(kOneSecond)));

        scoped.Reset(second.Start(tweens, Settings(kOneSecond)));

        EXPECT_EQ(first.cancelled, 1);
        EXPECT_EQ(second.cancelled, 0);
        EXPECT_FALSE(scoped.Get()->IsFinished());

        scoped.Reset();
        EXPECT_EQ(second.cancelled, 1);
        EXPECT_FALSE(scoped.Get());
    }

    TEST(TweenManagerTest, ScopedTweenMoveTransfersTheOwnership) {
        TweenManager tweens;
        Recorder recorder;
        ScopedTween target;
        {
            ScopedTween source(recorder.Start(tweens, Settings(kOneSecond)));
            target = std::move(source);
        }  // the moved-from one cancels nothing

        EXPECT_EQ(recorder.cancelled, 0);

        ScopedTween other = std::move(target);
        EXPECT_EQ(recorder.cancelled, 0);
        other.Reset();
        EXPECT_EQ(recorder.cancelled, 1);
    }

    TEST(TweenManagerTest, ScopedTweenOfACompletedTweenDoesNotCancel) {
        TweenManager tweens;
        Recorder recorder;
        {
            ScopedTween scoped(recorder.Start(tweens, Settings(kQuarterSecond)));
            tweens.Advance(kOneSecond);
        }

        EXPECT_EQ(recorder.completed, 1);
        EXPECT_EQ(recorder.cancelled, 0);
    }

    TEST(TweenManagerTest, DrivenByTheFrameTimeOfATimeObject) {
        TweenManager tweens;
        Time time;
        Recorder recorder;
        recorder.Start(tweens, Settings(kOneSecond));

        for (int frame = 0; frame < kFramesInASecond; ++frame) {
            time.Advance(1.0 / kFramesInASecond);
            tweens.Advance(time.GetDeltaTime());
        }

        EXPECT_EQ(recorder.values.size(), static_cast<size_t>(kFramesInASecond));
        EXPECT_EQ(Back(recorder.values), kTo);
        EXPECT_EQ(recorder.completed, 1);
    }

    TEST(TweenManagerTest, TenThousandTweensFinishInStartOrder) {
        constexpr int kCount = 10000;
        TweenManager tweens;
        Vector<int> order;
        order.reserve(kCount);
        for (int id = 0; id < kCount; ++id) {
            tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond), [](const float&) {}, [&order, id] { order.push_back(id); });
        }
        EXPECT_EQ(tweens.GetActiveCount(), static_cast<size_t>(kCount));

        tweens.Advance(kQuarterSecond);

        ASSERT_EQ(order.size(), static_cast<size_t>(kCount));
        for (int id = 0; id < kCount; ++id) {
            ASSERT_EQ(order[id], id);
        }
        EXPECT_EQ(tweens.GetActiveCount(), 0u);
    }

    TEST(TweenManagerTest, AnEmptySetterIsSkipped) {
        TweenManager tweens;
        int completed = 0;
        tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond), {}, [&] { ++completed; });

        tweens.Advance(kOneSecond);

        EXPECT_EQ(completed, 1);
    }

    TEST(TweenManagerTest, ANotFiniteDurationCountsAsZero) {
        for (const float duration : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
            TweenManager tweens;
            Recorder recorder;
            const auto handle = recorder.Start(tweens, Settings(duration));

            tweens.Advance(kSixtyFourthSecond);

            ASSERT_EQ(recorder.values.size(), 1u) << duration;
            EXPECT_EQ(recorder.values[0], kTo);
            EXPECT_EQ(recorder.completed, 1);
            EXPECT_EQ(handle->GetState(), TweenState::Completed);
            EXPECT_EQ(tweens.GetActiveCount(), 0u);
        }
    }

    TEST(TweenManagerTest, ANotFiniteDelayCountsAsZero) {
        for (const float delay : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            TweenManager tweens;
            Recorder recorder;
            const auto handle = recorder.Start(tweens, Settings(kQuarterSecond, delay));

            tweens.Advance(kQuarterSecond);

            ASSERT_EQ(recorder.values.size(), 1u) << delay;
            EXPECT_EQ(recorder.values[0], kTo);
            EXPECT_EQ(handle->GetState(), TweenState::Completed);
            EXPECT_EQ(recorder.completed, 1);
        }
    }

    TEST(TweenManagerTest, MoveOnlyCallbacksAreDestroyedOnceWhenTheTweenCompletes) {
        TweenManager tweens;
        Tallies tallies;
        const auto handle = StartTracked(tweens, tallies);
        ASSERT_EQ(tallies.value + tallies.complete + tallies.cancel, 0);

        tweens.Advance(kOneSecond);

        EXPECT_EQ(handle->GetState(), TweenState::Completed);
        EXPECT_EQ(tallies.value, 1);
        EXPECT_EQ(tallies.complete, 1);
        EXPECT_EQ(tallies.cancel, 1);
    }

    TEST(TweenManagerTest, MoveOnlyCallbacksAreDestroyedOnceWhenTheTweenIsCancelled) {
        TweenManager tweens;
        Tallies tallies;
        const auto handle = StartTracked(tweens, tallies);
        tweens.Advance(kQuarterSecond);

        handle->Cancel();

        EXPECT_TRUE(handle->IsCancelled());
        EXPECT_EQ(tallies.value, 1);
        EXPECT_EQ(tallies.complete, 1);
        EXPECT_EQ(tallies.cancel, 1);
    }

    TEST(TweenManagerTest, MoveOnlyCallbacksAreDestroyedOnceByCancelAllClearAndTheDestructorOfTheManager) {
        Tallies cancelAll;
        Tallies cleared;
        Tallies dropped;
        IntrusivePtr<TweenHandle> keptByTheCaller;
        {
            TweenManager tweens;
            StartTracked(tweens, cancelAll);
            tweens.CancelAll();
            EXPECT_EQ(cancelAll.value + cancelAll.complete + cancelAll.cancel, 3);

            StartTracked(tweens, cleared);
            tweens.Clear();
            EXPECT_EQ(cleared.value + cleared.complete + cleared.cancel, 3);

            keptByTheCaller = StartTracked(tweens, dropped);
            EXPECT_EQ(dropped.value + dropped.complete + dropped.cancel, 0);
        }

        EXPECT_EQ(dropped.value, 1);  // the manager is gone and the handle is not: the callables were released with the manager
        EXPECT_EQ(dropped.complete, 1);
        EXPECT_EQ(dropped.cancel, 1);
        EXPECT_TRUE(keptByTheCaller->IsCancelled());
        EXPECT_EQ(cancelAll.value, 1);
        EXPECT_EQ(cleared.cancel, 1);
    }

    TEST(TweenManagerTest, ATweenOfAPlayedMoveOnlyTimelineDestroysItsClipsOnce) {
        int clipEnded = 0;
        int completeEnded = 0;
        {
            TweenManager tweens;
            Timeline timeline;
            timeline.Append<float>(0.0f, kTo, Settings(kQuarterSecond), [token = DestroyTally(clipEnded)](const float&) {});
            tweens.Play(std::move(timeline), 0.0f, [token = DestroyTally(completeEnded)] {});

            tweens.Advance(kOneSecond);

            EXPECT_EQ(clipEnded, 1);  // the setter, which owns the timeline, was released at completion
            EXPECT_EQ(completeEnded, 1);
        }
        EXPECT_EQ(clipEnded, 1);
        EXPECT_EQ(completeEnded, 1);
    }

    TEST(TweenManagerTest, ASmallSetterAllocatesNothingBeyondTheHandleAndAPlayedTimelineOnlyItsOwnBox) {
        TweenManager tweens;
        float value = 0.0f;
        for (int warm = 0; warm < kWarmTweenCount; ++warm) {  // grows the lists of the manager
            tweens.Delay(kQuarterSecond);
        }
        tweens.Advance(kOneSecond);

        const uint64_t delayAllocations = AllocationsOf([&] { tweens.Delay(kOneSecond); });
        const uint64_t startAllocations = AllocationsOf([&] { tweens.Start<float>(0.0f, kTo, Settings(kOneSecond), [&value](const float& next) { value = next; }); });
        Timeline timeline;
        const uint64_t buildAllocations = AllocationsOf([&] { timeline.Append<float>(0.0f, kTo, Settings(kOneSecond), [&value](const float& next) { value = next; }); });
        const uint64_t playAllocations = AllocationsOf([&] { tweens.Play(std::move(timeline)); });

        if (AllocTracker::kEnabled) {
            EXPECT_EQ(startAllocations, delayAllocations);     // the handle, and nothing for the setter
            EXPECT_EQ(buildAllocations, 1u);                   // the storage of the clips
            EXPECT_EQ(playAllocations, delayAllocations + 1);  // the handle and the one box of the timeline: no control block
        } else {
            EXPECT_EQ(playAllocations, 0u);
        }
    }

    TEST(TweenManagerTest, ALoopRunsTurnAfterTurnAndCarriesTheTimePastTheEndOfATurn) {
        TweenManager tweens;
        Recorder recorder;
        const auto handle = recorder.Loop(tweens, Settings(kOneSecond, 0.0f, EaseType::QuadIn));
        const auto valueAt = [](float turnProgress) { return Lerp(0.0f, kTo, Ease(EaseType::QuadIn, turnProgress)); };

        tweens.Advance(kThreeEighthsSecond);
        tweens.Advance(kThreeEighthsSecond);
        tweens.Advance(kThreeEighthsSecond);  // 0.125 s into the second turn: the end of the first one is not written

        EXPECT_EQ(recorder.values, (Vector<float>{valueAt(0.375f), valueAt(0.75f), valueAt(0.125f)}));
        EXPECT_EQ(handle->GetProgress(), 0.125f);

        tweens.Advance(2.25f);   // over two turns in one step: one call, 0.375 s into the fourth turn
        tweens.Advance(0.625f);  // exactly to the end of that turn: the start of the next one

        ASSERT_EQ(recorder.values.size(), 5u);
        EXPECT_EQ(At(recorder.values, 3), valueAt(0.375f));
        EXPECT_EQ(At(recorder.values, 4), valueAt(0.0f));
        EXPECT_EQ(handle->GetProgress(), 0.0f);
    }

    TEST(TweenManagerTest, TheDelayOfALoopIsAppliedOnce) {
        TweenManager tweens;
        Recorder recorder;
        const auto handle = recorder.Loop(tweens, Settings(kOneSecond, kHalfSecond));

        tweens.Advance(kQuarterSecond);
        EXPECT_TRUE(recorder.values.empty());
        EXPECT_EQ(handle->GetState(), TweenState::Waiting);

        tweens.Advance(kHalfSecond);  // a quarter of a second past the delay
        tweens.Advance(kOneSecond);   // one whole turn later, with no second wait

        EXPECT_EQ(recorder.values, (Vector<float>{kQuarterTo, kQuarterTo}));
        EXPECT_EQ(handle->GetState(), TweenState::Running);
    }

    TEST(TweenManagerTest, LoopsKeepTheirPlaceInTheWalkThroughTurnsAndACompaction) {
        TweenManager tweens;
        Vector<int> calls;
        Vector<IntrusivePtr<TweenHandle>> neighbours;
        Vector<int> startOrder;
        for (int number = 0; number < kNeighbourCount; ++number) {
            neighbours.push_back(tweens.Delay(kHugeFrame));
            if (number % kNeighboursPerLoop == 0) {
                const int loop = number / kNeighboursPerLoop;
                startOrder.push_back(loop);
                tweens.Loop<float>(0.0f, kTo, Settings(kQuarterSecond * static_cast<float>(1 + loop % kTurnLengths)), [&calls, loop](const float&) { calls.push_back(loop); });
            }
        }
        const auto walkOfOneFrame = [&] {
            calls.clear();
            tweens.Advance(kThreeEighthsSecond);
            return calls;
        };

        for (int frame = 0; frame < kManyFrames; ++frame) {
            ASSERT_EQ(walkOfOneFrame(), startOrder) << "frame " << frame;
        }
        for (const IntrusivePtr<TweenHandle>& neighbour : neighbours) {
            neighbour->Cancel();
        }
        for (int frame = 0; frame < kManyFrames; ++frame) {  // the first of them compacts: every loop moves to another slot
            ASSERT_EQ(walkOfOneFrame(), startOrder) << "frame after the cancel " << frame;
        }
        EXPECT_EQ(tweens.GetActiveCount(), static_cast<size_t>(kLoopCount));
    }

    TEST(TweenManagerTest, ALoopNeverCompletesAndOnlyACancelEndsItAndItsAwait) {
        TweenManager tweens;
        Recorder recorder;
        const auto handle = recorder.Loop(tweens, Settings(kQuarterSecond));
        Vector<TweenResult> results;
        Task<void> task = AwaitResult(handle, results);
        task.Resume();

        for (int frame = 0; frame < kManyFrames; ++frame) {
            tweens.Advance(kHugeFrame);
        }

        EXPECT_EQ(handle->GetState(), TweenState::Running);
        EXPECT_EQ(tweens.GetActiveCount(AnimationFrameClass::DecorativeLoop), 1u);
        EXPECT_TRUE(results.empty());

        handle->Cancel();
        handle->Cancel();
        tweens.Advance(kQuarterSecond);

        EXPECT_TRUE(handle->IsCancelled());
        EXPECT_EQ(recorder.values.size(), static_cast<size_t>(kManyFrames));
        EXPECT_EQ(recorder.cancelled, 1);
        EXPECT_EQ(results, (Vector<TweenResult>{TweenResult::Cancelled}));
        EXPECT_EQ(tweens.GetActiveCount(), 0u);
    }

    TEST(TweenManagerTest, ALoopMayBeCancelledByItsOwnSetter) {
        TweenManager tweens;
        IntrusivePtr<TweenHandle> handle;
        int calls = 0;
        int cancelled = 0;
        handle = tweens.Loop<float>(
            0.0f, kTo, Settings(kQuarterSecond),
            [&](const float&) {
                if (++calls == kLaterTweenCount) {
                    handle->Cancel();
                }
            },
            [&cancelled] { ++cancelled; });

        for (int frame = 0; frame < kManyFrames; ++frame) {
            tweens.Advance(kThreeEighthsSecond);
        }

        EXPECT_EQ(calls, kLaterTweenCount);
        EXPECT_EQ(cancelled, 1);
        EXPECT_TRUE(handle->IsCancelled());
    }

    TEST(TweenManagerTest, ALoopOfNoLengthIsOneOrdinaryStep) {
        for (const float duration : {0.0f, -kOneSecond, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            TweenManager tweens;
            Recorder recorder;
            const auto handle = recorder.Loop(tweens, Settings(duration));
            EXPECT_EQ(handle->GetFrameClass(), AnimationFrameClass::FiniteEffect);

            tweens.Advance(kSixtyFourthSecond);
            tweens.Advance(kSixtyFourthSecond);

            EXPECT_EQ(recorder.values, (Vector<float>{kTo})) << duration;
            EXPECT_EQ(handle->GetState(), TweenState::Completed);
            EXPECT_EQ(recorder.cancelled, 0);
        }
    }

    TEST(TweenManagerTest, AnInfiniteFrameLeavesNoNaNInALoop) {
        for (const float delay : {0.0f, kQuarterSecond}) {
            TweenManager tweens;
            Recorder recorder;
            const auto handle = recorder.Loop(tweens, Settings(kOneSecond, delay));

            tweens.Advance(std::numeric_limits<float>::infinity());
            EXPECT_EQ(handle->GetProgress(), 0.0f) << delay;
            tweens.Advance(kQuarterSecond);  // the time in the slot is finite again, so the loop goes on

            EXPECT_EQ(recorder.values, (Vector<float>{0.0f, kQuarterTo})) << delay;
        }
    }

    TEST(TweenManagerTest, TheTurnsOfALoopMakeNoHandleAndAllocateNothing) {
        TweenManager tweens;
        float value = 0.0f;
        for (int loop = 0; loop < kLoopCount; ++loop) {
            tweens.Loop<float>(0.0f, kTo, Settings(kQuarterSecond * static_cast<float>(1 + loop % kTurnLengths)), [&value](const float& next) { value = next; });
        }
        tweens.Advance(kOneSecond);
        const uint64_t handles = PoolAllocations("TweenHandle");  // a restart makes a handle, a turn of a loop does not
        ASSERT_NE(handles, 0u);

        const uint64_t allocations = AllocationsOf([&] {
            for (int frame = 0; frame < kManyFrames; ++frame) {
                tweens.Advance(kThreeEighthsSecond);
            }
        });

        EXPECT_EQ(PoolAllocations("TweenHandle"), handles);
        EXPECT_EQ(allocations, 0u);  // always zero without the tracker: the handle count above is the check there
    }

}  // namespace BECore::Tests
