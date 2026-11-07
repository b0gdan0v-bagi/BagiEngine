#include <BECore/Animation/ScopedTween.h>
#include <BECore/Animation/TweenManager.h>
#include <BECore/Time/Time.h>
#include <TaskSystem/Task.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        // Frame times and durations are powers of two, so the sums are exact in floating point
        constexpr float kOneSecond = 1.0f;
        constexpr float kHalfSecond = 0.5f;
        constexpr float kQuarterSecond = 0.25f;
        constexpr float kThreeQuartersSecond = 0.75f;
        constexpr float kThreeEighthsSecond = 0.375f;  // 1.5 clips of kQuarterSecond: a frame that ends in the middle of a clip
        constexpr float kTo = 100.0f;
        constexpr int kClipCount = 4;
        constexpr int kFramesForTheSequence = 3;  // 4 clips of 0.25 s at 0.375 s a frame
        constexpr int kFramesForTheChain = 4;     // the same four tweens chained by onComplete: one frame per link
        constexpr int kFramesInASecond = 32;
        constexpr int kManyTimelines = 1000;
        constexpr int kTwoRepeats = 2;
        constexpr int kNeverCalled = -1;

        TweenSettings Settings(float duration, float delay = 0.0f) {
            return {.duration = duration, .delay = delay};
        }

        /// What the clips of a timeline wrote: `(clip id, value)` in the order of the calls
        using Calls = eastl::vector<eastl::pair<int, float>>;

        /// A sequence of @p kClipCount clips of a quarter second, clip `i` writes to @p calls with its id
        Timeline MakeSequence(Calls& calls) {
            Timeline timeline;
            for (int id = 0; id < kClipCount; ++id) {
                timeline.Append<float>(0.0f, kTo, Settings(kQuarterSecond), [&calls, id](const float& value) { calls.push_back({id, value}); });
            }
            return timeline;
        }

        /// A timeline of one tween of @p duration seconds that writes its value to @p value
        Timeline MakeTween(float& value, float duration) {
            Timeline timeline;
            timeline.Append<float>(0.0f, kTo, Settings(duration), [&value](const float& next) { value = next; });
            return timeline;
        }

        /// The ids of the clips whose last value is exactly the target, in the order of those calls
        eastl::vector<int> FinishedIds(const Calls& calls) {
            eastl::vector<int> ids;
            for (const auto& call : calls) {
                if (call.second == kTo) {
                    ids.push_back(call.first);
                }
            }
            return ids;
        }

        /// Starts one tween of @p link seconds after another through `onComplete`, as many as @p remaining
        void ChainTweens(TweenManager& tweens, int remaining, int& finished) {
            if (remaining == 0) {
                return;
            }
            tweens.Start<float>(0.0f, kTo, Settings(kQuarterSecond), {}, [&tweens, remaining, &finished] {
                ++finished;
                ChainTweens(tweens, remaining - 1, finished);
            });
        }

        struct Log {
            eastl::vector<TweenResult> results;
            int stage = 0;
        };

        Task<void> AwaitTimeline(IntrusivePtr<TweenHandle> handle, Log& log) {
            log.results.push_back(co_await AwaitTween(std::move(handle)));
        }

        /// Plays two timelines in a row and awaits each
        Task<void> PlayTwo(TweenManager& tweens, float& first, float& second, Log& log) {
            log.results.push_back(co_await AwaitTween(tweens.Play(MakeTween(first, kQuarterSecond))));
            log.stage = 1;
            log.results.push_back(co_await AwaitTween(tweens.Play(MakeTween(second, kQuarterSecond))));
            log.stage = 2;
        }

    }  // namespace

    TEST(TimelinePlayTest, TheTimeCarriesBetweenClips) {
        TweenManager tweens;
        Calls calls;
        int completed = 0;
        const auto handle = tweens.Play(MakeSequence(calls), 0.0f, [&completed] { ++completed; });

        for (int frame = 0; frame < kFramesForTheSequence - 1; ++frame) {
            tweens.Advance(kThreeEighthsSecond);
            EXPECT_FALSE(handle->IsFinished()) << frame;
        }
        tweens.Advance(kThreeEighthsSecond);

        EXPECT_TRUE(handle->IsFinished());
        EXPECT_EQ(handle->GetState(), TweenState::Completed);
        EXPECT_EQ(completed, 1);
        EXPECT_EQ(FinishedIds(calls), (eastl::vector<int>{0, 1, 2, 3}));  // every clip ended on its value, in order
        EXPECT_EQ(tweens.GetActiveCount(), 0u);
    }

    TEST(TimelinePlayTest, ChainedTweensLoseTheLeftOverTimeAndNeedMoreFrames) {
        TweenManager tweens;
        int finished = 0;
        ChainTweens(tweens, kClipCount, finished);

        for (int frame = 0; frame < kFramesForTheSequence; ++frame) {
            tweens.Advance(kThreeEighthsSecond);
        }
        EXPECT_LT(finished, kClipCount);  // the timeline of the test above is done in these frames

        tweens.Advance(kThreeEighthsSecond);
        EXPECT_EQ(finished, kClipCount);
        EXPECT_EQ(kFramesForTheChain, kFramesForTheSequence + 1);
    }

    TEST(TimelinePlayTest, NoCallDuringTheDelay) {
        TweenManager tweens;
        Calls calls;
        const auto handle = tweens.Play(MakeSequence(calls), kOneSecond);

        tweens.Advance(kHalfSecond);
        tweens.Advance(kQuarterSecond);

        EXPECT_TRUE(calls.empty());
        EXPECT_EQ(handle->GetState(), TweenState::Waiting);

        tweens.Advance(kQuarterSecond);  // the delay is over exactly now: the timeline starts from its beginning
        tweens.Advance(kOneSecond);
        EXPECT_TRUE(handle->IsFinished());
        EXPECT_EQ(FinishedIds(calls), (eastl::vector<int>{0, 1, 2, 3}));
    }

    TEST(TimelinePlayTest, CompletionRunsOnceAfterTheLastCall) {
        TweenManager tweens;
        float value = kNeverCalled;
        int completed = 0;
        float valueAtCompletion = kNeverCalled;
        tweens.Play(MakeTween(value, kHalfSecond), 0.0f, [&] {
            ++completed;
            valueAtCompletion = value;
        });

        tweens.Advance(kQuarterSecond);
        EXPECT_EQ(completed, 0);
        tweens.Advance(kQuarterSecond);
        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(completed, 1);
        EXPECT_EQ(valueAtCompletion, kTo);
    }

    TEST(TimelinePlayTest, CancelRunsTheCancelCallbackOnceAndTheTimelineStaysWhereItWas) {
        TweenManager tweens;
        Calls calls;
        int completed = 0;
        int cancelled = 0;
        const auto handle = tweens.Play(MakeSequence(calls), 0.0f, [&completed] { ++completed; }, [&cancelled] { ++cancelled; });
        tweens.Advance(kThreeEighthsSecond);
        const Calls before = calls;

        handle->Cancel();
        handle->Cancel();
        tweens.Advance(kOneSecond);

        EXPECT_EQ(cancelled, 1);
        EXPECT_EQ(completed, 0);
        EXPECT_EQ(handle->GetState(), TweenState::Cancelled);
        EXPECT_EQ(calls, before);  // no more calls, no value set
    }

    TEST(TimelinePlayTest, CancelAllCancelsWithCallbacksAndClearDropsWithout) {
        TweenManager tweens;
        Calls calls;
        int cancelled = 0;
        const auto first = tweens.Play(MakeSequence(calls), 0.0f, {}, [&cancelled] { ++cancelled; });
        const auto second = tweens.Play(MakeSequence(calls), 0.0f, {}, [&cancelled] { ++cancelled; });
        tweens.CancelAll();
        EXPECT_EQ(cancelled, 2);
        EXPECT_TRUE(first->IsCancelled());
        EXPECT_TRUE(second->IsCancelled());

        const auto third = tweens.Play(MakeSequence(calls), 0.0f, {}, [&cancelled] { ++cancelled; });
        tweens.Clear();
        EXPECT_EQ(cancelled, 2);  // no callback
        EXPECT_TRUE(third->IsCancelled());
        EXPECT_TRUE(calls.empty());
    }

    TEST(TimelinePlayTest, ATimelineStartedInACallbackRunsFromTheNextAdvance) {
        TweenManager tweens;
        float value = kNeverCalled;
        IntrusivePtr<TweenHandle> inner;
        tweens.Delay(kQuarterSecond, [&] { inner = tweens.Play(MakeTween(value, kQuarterSecond)); });

        tweens.Advance(kQuarterSecond);
        ASSERT_TRUE(inner);
        EXPECT_EQ(value, kNeverCalled);  // not in the Advance that started it
        EXPECT_EQ(inner->GetState(), TweenState::Waiting);

        tweens.Advance(kQuarterSecond);
        EXPECT_EQ(value, kTo);
        EXPECT_TRUE(inner->IsFinished());
    }

    TEST(TimelinePlayTest, TheProgressOfTheHandleIsTheProgressOfTheTimeline) {
        TweenManager tweens;
        Calls calls;
        const auto handle = tweens.Play(MakeSequence(calls));
        EXPECT_EQ(handle->GetProgress(), 0.0f);

        tweens.Advance(kQuarterSecond);
        EXPECT_EQ(handle->GetProgress(), kQuarterSecond);
        tweens.Advance(kHalfSecond);
        EXPECT_EQ(handle->GetProgress(), kThreeQuartersSecond);
        tweens.Advance(kOneSecond);
        EXPECT_EQ(handle->GetProgress(), 1.0f);
    }

    TEST(TimelinePlayTest, AScopedTweenCancelsAPlayingTimeline) {
        TweenManager tweens;
        Calls calls;
        int cancelled = 0;
        {
            ScopedTween scoped(tweens.Play(MakeSequence(calls), 0.0f, {}, [&cancelled] { ++cancelled; }));
            tweens.Advance(kQuarterSecond);
        }
        const Calls before = calls;
        tweens.Advance(kOneSecond);

        EXPECT_EQ(cancelled, 1);
        EXPECT_EQ(calls, before);
        EXPECT_EQ(tweens.GetActiveCount(), 0u);
    }

    TEST(TimelinePlayTest, AnAwaitResumesInTheAdvanceThatFinishesTheTimeline) {
        TweenManager tweens;
        Calls calls;
        Log log;
        Task<void> task = AwaitTimeline(tweens.Play(MakeSequence(calls)), log);
        task.Resume();

        for (int frame = 0; frame < kFramesForTheSequence - 1; ++frame) {
            tweens.Advance(kThreeEighthsSecond);
            EXPECT_TRUE(log.results.empty()) << frame;
        }
        tweens.Advance(kThreeEighthsSecond);

        ASSERT_EQ(log.results.size(), 1u);
        EXPECT_EQ(log.results[0], TweenResult::Completed);
        EXPECT_TRUE(task.IsDone());
    }

    TEST(TimelinePlayTest, AnAwaitReadsCancelledWhenTheTimelineIsCancelled) {
        TweenManager tweens;
        Calls calls;
        Log log;
        const auto handle = tweens.Play(MakeSequence(calls));
        Task<void> task = AwaitTimeline(handle, log);
        task.Resume();
        tweens.Advance(kQuarterSecond);

        handle->Cancel();

        ASSERT_EQ(log.results.size(), 1u);
        EXPECT_EQ(log.results[0], TweenResult::Cancelled);
    }

    TEST(TimelinePlayTest, ACoroutineThatPlaysTwoTimelinesInARowTakesOneAdvancePerLink) {
        TweenManager tweens;
        float first = kNeverCalled;
        float second = kNeverCalled;
        Log log;
        Task<void> task = PlayTwo(tweens, first, second, log);
        task.Resume();

        tweens.Advance(kQuarterSecond);  // the first one ends, the coroutine plays the second
        EXPECT_EQ(first, kTo);
        EXPECT_EQ(log.stage, 1);
        EXPECT_EQ(second, kNeverCalled);

        tweens.Advance(kQuarterSecond);
        EXPECT_EQ(second, kTo);
        EXPECT_EQ(log.stage, 2);
        EXPECT_EQ(log.results, (eastl::vector<TweenResult>{TweenResult::Completed, TweenResult::Completed}));
        EXPECT_TRUE(task.IsDone());
    }

    TEST(TimelinePlayTest, RepeatTwiceTakesTwiceTheTimeAndStartsFromFromAgain) {
        TweenManager tweens;
        eastl::vector<float> values;
        Timeline timeline;
        timeline.Append<float>(0.0f, kTo, Settings(kHalfSecond), [&values](const float& value) { values.push_back(value); }).Repeat(kTwoRepeats);
        ASSERT_EQ(timeline.GetDuration(), kOneSecond);
        const auto handle = tweens.Play(std::move(timeline));

        tweens.Advance(kHalfSecond);
        EXPECT_FALSE(handle->IsFinished());
        tweens.Advance(kQuarterSecond);
        EXPECT_FALSE(handle->IsFinished());
        tweens.Advance(kQuarterSecond);

        EXPECT_TRUE(handle->IsFinished());
        // The first iteration ends on kTo at half a second, the second one starts again from `from` and ends on kTo
        EXPECT_EQ(values, (eastl::vector<float>{kTo, 0.0f, kTo / 2, kTo}));
    }

    TEST(TimelinePlayTest, ATimelineWithNoLengthCompletesOnTheFirstAdvanceAfterTheDelay) {
        TweenManager tweens;
        int fired = 0;
        Timeline timeline;
        timeline.AppendCallback([&fired] { ++fired; });
        int completed = 0;
        const auto handle = tweens.Play(std::move(timeline), kQuarterSecond, [&completed] { ++completed; });

        tweens.Advance(kQuarterSecond);  // the delay ends exactly now: rule 3 applies the time left after it, which is none
        EXPECT_EQ(fired, 1);
        EXPECT_EQ(completed, 1);
        EXPECT_EQ(handle->GetState(), TweenState::Completed);
    }

    TEST(TimelinePlayTest, TwoFrameRatesOfATimeObjectEndWithTheSameValues) {
        constexpr int kFrameRatesToCompare[] = {kFramesInASecond, kFramesInASecond / 4};
        eastl::vector<eastl::vector<int>> finishedByRate;
        for (const int framesPerSecond : kFrameRatesToCompare) {
            TweenManager tweens;
            Time time;
            Calls calls;
            const auto handle = tweens.Play(MakeSequence(calls));
            for (int frame = 0; frame < framesPerSecond; ++frame) {
                time.Advance(1.0 / framesPerSecond);
                tweens.Advance(time.GetDeltaTime());
            }
            EXPECT_TRUE(handle->IsFinished()) << framesPerSecond;
            EXPECT_EQ(calls.back().second, kTo) << framesPerSecond;
            finishedByRate.push_back(FinishedIds(calls));
        }
        EXPECT_EQ(finishedByRate[0], (eastl::vector<int>{0, 1, 2, 3}));
        EXPECT_EQ(finishedByRate[0], finishedByRate[1]);
    }

    TEST(TimelinePlayTest, ManyTimelinesInOneAdvanceFinishInStartOrder) {
        TweenManager tweens;
        eastl::vector<int> order;
        for (int index = 0; index < kManyTimelines; ++index) {
            Timeline timeline;
            timeline.AppendCallback([&order, index] { order.push_back(index); });
            timeline.AppendDelay(kQuarterSecond);
            tweens.Play(std::move(timeline));
        }

        tweens.Advance(kOneSecond);

        ASSERT_EQ(order.size(), static_cast<size_t>(kManyTimelines));
        for (int index = 0; index < kManyTimelines; ++index) {
            EXPECT_EQ(order[index], index);
        }
        EXPECT_EQ(tweens.GetActiveCount(), 0u);
    }

}  // namespace BECore::Tests
