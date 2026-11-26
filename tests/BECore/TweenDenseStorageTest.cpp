#include <BECore/Animation/Timeline.h>
#include <BECore/Animation/TweenManager.h>
#include <Common/AllocCheck.h>
#include <Common/DestroyTally.h>
#include <Common/Require.h>
#include <TaskSystem/Task.h>
#include <gtest/gtest.h>

// The edge cases of the storage of `TweenManager` (hot slots in a dense array, handles on an id, tombstones that are compacted lazily): what a
// slot that moved, an id that was reused or a list that grows under a callback could get wrong. Rules 1 to 10 of `Animation/CLAUDE.md`.
namespace BECore::Tests {

    namespace {

        // Frame times and durations are powers of two, so the sums are exact in floating point
        constexpr float kQuarterSecond = 0.25f;
        constexpr float kHalfSecond = 0.5f;
        constexpr float kOneSecond = 1.0f;
        constexpr float kLongSecond = 4.0f;
        constexpr float kSixteenthSecond = 0.0625f;

        // An event of a numbered tween is its number (a setter call), `kCompleteBase + number` (its completion) or `kCancelBase + number`
        constexpr int kCompleteBase = 1000;
        constexpr int kCancelBase = 2000;
        constexpr int kNumberRange = 1000;

        // More tombstones than a walk tolerates before it compacts (and fewer than that), the batch sizes chosen around that threshold
        constexpr int kBigBatch = 200;
        constexpr int kEndedBatch = 70;
        constexpr int kFewEnded = 50;
        constexpr int kManyLive = 300;
        constexpr int kCancelledLive = 20;
        constexpr int kSteadyCount = 1000;

        TweenSettings Settings(float duration, float delay = 0.0f) {
            return {.duration = duration, .delay = delay};
        }

        /// A float tween whose setter, completion and cancel write their event into @p events
        IntrusivePtr<TweenHandle> StartNumbered(TweenManager& tweens, Vector<int>& events, int number, float duration, float delay = 0.0f) {
            return tweens.Start<float>(
                0.0f, 1.0f, Settings(duration, delay), [&events, number](const float&) { events.push_back(number); }, [&events, number] { events.push_back(kCompleteBase + number); },
                [&events, number] { events.push_back(kCancelBase + number); });
        }

        /// The tween that has no setter
        IntrusivePtr<TweenHandle> StartSilent(TweenManager& tweens, float duration, MoveOnlyFunction<void()> onComplete = {}, MoveOnlyFunction<void()> onCancel = {}) {
            return tweens.Start<float>(0.0f, 1.0f, Settings(duration), {}, std::move(onComplete), std::move(onCancel));
        }

        /// The three callables of a tween, each holding a move-only token that counts its end
        struct Tallies {
            int value = 0;
            int complete = 0;
            int cancel = 0;
        };

        IntrusivePtr<TweenHandle> StartTracked(TweenManager& tweens, Tallies& tallies) {
            return tweens.Start<float>(
                0.0f, 1.0f, Settings(kOneSecond), [token = DestroyTally(tallies.value)](const float&) {}, [token = DestroyTally(tallies.complete)] {}, [token = DestroyTally(tallies.cancel)] {});
        }

        int NumberOf(int event) {
            return event % kNumberRange;
        }

        /// Every event is of a tween that did not start before the tween of the one before: the order of one walk (rule 1)
        bool InStartOrder(const Vector<int>& events) {
            for (size_t i = 1; i < events.size(); ++i) {
                if (NumberOf(events[i]) < NumberOf(events[i - 1])) {
                    return false;
                }
            }
            return true;
        }

        int Count(const Vector<int>& events, int event) {
            return static_cast<int>(eastl::count(events.begin(), events.end(), event));
        }

        Task<void> AwaitHandle(IntrusivePtr<TweenHandle> handle, int& resumed) {
            co_await AwaitTween(std::move(handle));
            ++resumed;
        }

    }  // namespace

    TEST(TweenDenseStorageTest, CallbacksAndSettersRunInStartOrderAfterCompactionsAndIdReuse) {
        TweenManager tweens;
        Vector<int> events;
        Vector<IntrusivePtr<TweenHandle>> handles;
        constexpr int kFramesOfTheBatches = 5;
        for (int number = 0; number < kBigBatch; ++number) {
            handles.push_back(StartNumbered(tweens, events, number, kQuarterSecond * static_cast<float>(1 + number % kFramesOfTheBatches)));
        }

        for (int frame = 1; frame <= 2 * kFramesOfTheBatches; ++frame) {
            events.clear();
            tweens.Advance(kQuarterSecond);
            EXPECT_TRUE(InStartOrder(events)) << "frame " << frame;
            if (frame == 2) {
                // by now the ends of two frames were compacted away: the new tweens take the ids that are free, and still run after the older ones
                for (int number = kBigBatch; number < 2 * kBigBatch; ++number) {
                    handles.push_back(StartNumbered(tweens, events, number, kQuarterSecond * static_cast<float>(1 + number % 3)));
                }
            }
        }

        EXPECT_EQ(tweens.GetActiveCount(), 0u);
        for (int number = 0; number < 2 * kBigBatch; ++number) {
            EXPECT_EQ(handles[static_cast<size_t>(number)]->GetState(), TweenState::Completed) << number;
        }
    }

    TEST(TweenDenseStorageTest, AHandleReadsItsOwnTweenAfterItsSlotMoved) {
        TweenManager tweens;
        Vector<int> events;
        Vector<IntrusivePtr<TweenHandle>> handles;
        constexpr int kEnding = 90;
        constexpr int kRunning = 10;
        for (int number = 0; number < kEnding; ++number) {
            handles.push_back(StartNumbered(tweens, events, number, kQuarterSecond));
        }
        for (int number = kEnding; number < kEnding + kRunning; ++number) {
            handles.push_back(StartNumbered(tweens, events, number, kOneSecond));
        }
        const auto waiting = StartNumbered(tweens, events, kEnding + kRunning, kOneSecond, kOneSecond);

        tweens.Advance(kQuarterSecond);  // the ended ones are many: the survivors move to the front

        EXPECT_EQ(handles[0]->GetState(), TweenState::Completed);
        EXPECT_EQ(handles[0]->GetProgress(), 1.0f);
        EXPECT_EQ(waiting->GetState(), TweenState::Waiting);
        EXPECT_EQ(waiting->GetProgress(), 0.0f);
        for (int number = kEnding; number < kEnding + kRunning; ++number) {
            EXPECT_EQ(handles[static_cast<size_t>(number)]->GetState(), TweenState::Running) << number;
            EXPECT_EQ(handles[static_cast<size_t>(number)]->GetProgress(), 0.25f) << number;
        }

        const auto victim = handles[kEnding + 5];
        victim->Cancel();
        // A new tween takes an id that is free; the finished handle that once held it must not reach it
        const auto fresh = StartNumbered(tweens, events, kEnding + kRunning + 1, kOneSecond);
        handles[0]->Cancel();
        events.clear();
        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(victim->GetState(), TweenState::Cancelled);
        EXPECT_EQ(victim->GetProgress(), 0.25f);  // where it stopped
        EXPECT_EQ(Count(events, kEnding + 5), 0);
        EXPECT_EQ(Count(events, kCancelBase + kEnding + kRunning + 1), 0);
        EXPECT_EQ(fresh->GetState(), TweenState::Running);
        EXPECT_EQ(fresh->GetProgress(), 0.25f);
        EXPECT_EQ(handles[kEnding]->GetProgress(), 0.5f);
        EXPECT_EQ(handles[0]->GetState(), TweenState::Completed);
    }

    TEST(TweenDenseStorageTest, ACancelInACallbackSkipsATweenThatHasNotRunYet) {
        TweenManager tweens;
        Vector<int> events;
        IntrusivePtr<TweenHandle> victim;
        StartSilent(tweens, kQuarterSecond, [&victim] { victim->Cancel(); });
        victim = StartNumbered(tweens, events, 1, kOneSecond);

        tweens.Advance(kQuarterSecond);
        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(events, (Vector<int>{kCancelBase + 1}));  // no setter call, one cancel
        EXPECT_EQ(victim->GetState(), TweenState::Cancelled);
    }

    TEST(TweenDenseStorageTest, ACancelInACallbackStopsATweenThatRanAlready) {
        TweenManager tweens;
        Vector<int> events;
        const auto victim = StartNumbered(tweens, events, 0, kOneSecond);
        StartSilent(tweens, kQuarterSecond, [&victim] { victim->Cancel(); });

        tweens.Advance(kQuarterSecond);
        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(events, (Vector<int>{0, kCancelBase}));
        EXPECT_EQ(victim->GetState(), TweenState::Cancelled);
    }

    TEST(TweenDenseStorageTest, ASetterThatCancelsItsOwnTweenIsDestroyedAfterItReturns) {
        struct Probe {
            TweenManager* tweens = nullptr;
            IntrusivePtr<TweenHandle> self;
            IntrusivePtr<TweenHandle> successor;
            Vector<int> events;
            int calls = 0;
            int endedAtCancel = -1;
            int ended = 0;
            int cancels = 0;
        };
        TweenManager tweens;
        Probe probe{.tweens = &tweens};
        probe.self = tweens.Start<float>(
            0.0f, 1.0f, Settings(kOneSecond),
            [&probe, token = DestroyTally(probe.ended)](const float&) {
                ++probe.calls;
                probe.self->Cancel();
                probe.endedAtCancel = probe.ended;  // the capture is still alive: the setter is mid-call
                // started in the setter that just freed its own id: the new tween must not be taken for it
                probe.successor = StartNumbered(*probe.tweens, probe.events, 7, kHalfSecond);
            },
            {}, [&probe] { ++probe.cancels; });

        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(probe.calls, 1);
        EXPECT_EQ(probe.endedAtCancel, 0);
        EXPECT_EQ(probe.ended, 1);
        EXPECT_EQ(probe.cancels, 1);
        EXPECT_EQ(probe.self->GetState(), TweenState::Cancelled);
        EXPECT_EQ(probe.successor->GetState(), TweenState::Waiting);

        tweens.Advance(kQuarterSecond);
        probe.self->Cancel();

        EXPECT_EQ(probe.events, (Vector<int>{7}));
        EXPECT_EQ(probe.successor->GetState(), TweenState::Running);
        EXPECT_EQ(probe.cancels, 1);
        EXPECT_EQ(probe.calls, 1);
    }

    TEST(TweenDenseStorageTest, ACallbackCancelsAFinishedTweenAndALiveNeighbourAmongTombstones) {
        TweenManager tweens;
        Vector<int> events;
        Vector<IntrusivePtr<TweenHandle>> handles;
        constexpr int kEnded = 10;
        for (int number = 0; number < kEnded; ++number) {
            handles.push_back(StartNumbered(tweens, events, number, kQuarterSecond));
        }
        // after the ended ones (tombstones until a compaction): one that cancels a tombstone and its live neighbour that has not run yet
        handles.push_back(tweens.Start<float>(0.0f, 1.0f, Settings(kHalfSecond), {}, [&handles] {
            handles[3]->Cancel();
            handles[kEnded + 1]->Cancel();
        }));
        handles.push_back(StartNumbered(tweens, events, kEnded + 1, kOneSecond));
        tweens.Advance(kQuarterSecond);
        events.clear();

        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(events, (Vector<int>{kCancelBase + kEnded + 1}));  // the tombstone's cancel is nothing; the neighbour never ran in this walk
        EXPECT_EQ(handles[3]->GetState(), TweenState::Completed);
        EXPECT_EQ(handles[kEnded + 1]->GetState(), TweenState::Cancelled);
        EXPECT_EQ(tweens.GetActiveCount(), 0u);
    }

    TEST(TweenDenseStorageTest, ATweenStartedInACallbackFirstRunsInTheNextAdvanceAfterTheOlderOnes) {
        TweenManager tweens;
        Vector<int> events;
        IntrusivePtr<TweenHandle> started;
        StartSilent(tweens, kQuarterSecond, [&] { started = StartNumbered(tweens, events, 9, kHalfSecond); });
        StartNumbered(tweens, events, 1, kOneSecond);
        StartNumbered(tweens, events, 2, kOneSecond);

        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(events, (Vector<int>{1, 2}));
        EXPECT_EQ(started->GetState(), TweenState::Waiting);
        EXPECT_EQ(tweens.GetActiveCount(), 3u);

        events.clear();
        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(events, (Vector<int>{1, 2, 9}));
        EXPECT_EQ(started->GetProgress(), 0.5f);
    }

    TEST(TweenDenseStorageTest, TweensStartedInAWalkThatCompactsRunInStartOrderAfterTheSurvivors) {
        TweenManager tweens;
        Vector<int> events;
        Vector<IntrusivePtr<TweenHandle>> successors;
        constexpr int kSuccessorBase = 100;
        StartNumbered(tweens, events, 0, 2.0f);
        for (int number = 1; number <= kEndedBatch; ++number) {
            StartSilent(tweens, kQuarterSecond, [&, number] { successors.push_back(StartNumbered(tweens, events, kSuccessorBase + number, kHalfSecond)); });
        }

        tweens.Advance(kQuarterSecond);  // every ended one is a tombstone past the threshold: the walk compacts and merges what was started
        events.clear();
        tweens.Advance(kQuarterSecond);

        Vector<int> expected{0};
        for (int number = 1; number <= kEndedBatch; ++number) {
            expected.push_back(kSuccessorBase + number);
        }
        EXPECT_EQ(events, expected);
        EXPECT_EQ(tweens.GetActiveCount(), static_cast<size_t>(kEndedBatch + 1));
    }

    TEST(TweenDenseStorageTest, CancelAllCancelsInStartOrderAndSpareATweenStartedByACancelCallback) {
        TweenManager tweens;
        Vector<int> events;
        IntrusivePtr<TweenHandle> extra;
        const auto first = tweens.Start<float>(0.0f, 1.0f, Settings(kOneSecond), {}, {}, [&] {
            events.push_back(kCancelBase);
            extra = StartNumbered(tweens, events, 9, kOneSecond);
        });
        StartNumbered(tweens, events, 1, kOneSecond);
        StartNumbered(tweens, events, 2, kOneSecond);

        tweens.CancelAll();

        EXPECT_EQ(events, (Vector<int>{kCancelBase, kCancelBase + 1, kCancelBase + 2}));
        EXPECT_EQ(first->GetState(), TweenState::Cancelled);
        EXPECT_EQ(extra->GetState(), TweenState::Waiting);
        EXPECT_EQ(tweens.GetActiveCount(), 1u);

        events.clear();
        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(events, (Vector<int>{9}));
    }

    TEST(TweenDenseStorageTest, CancelAllFromACallbackCancelsWhatStartedBeforeTheCallAndSparesWhatStartedAfter) {
        TweenManager tweens;
        Vector<int> events;
        IntrusivePtr<TweenHandle> before;
        IntrusivePtr<TweenHandle> after;
        StartSilent(tweens, kQuarterSecond, [&] {
            before = StartNumbered(tweens, events, 5, kOneSecond);
            tweens.CancelAll();
            after = StartNumbered(tweens, events, 6, kOneSecond);
        });
        StartNumbered(tweens, events, 1, kOneSecond);
        StartNumbered(tweens, events, 2, kOneSecond);

        tweens.Advance(kQuarterSecond);

        // the two that were still to run in this walk do not run; the cancel callbacks are in start order, the pending one last
        EXPECT_EQ(events, (Vector<int>{kCancelBase + 1, kCancelBase + 2, kCancelBase + 5}));
        EXPECT_EQ(before->GetState(), TweenState::Cancelled);
        EXPECT_EQ(after->GetState(), TweenState::Waiting);
        EXPECT_EQ(tweens.GetActiveCount(), 1u);

        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(Count(events, 6), 1);
    }

    TEST(TweenDenseStorageTest, ClearInAWalkDropsEverythingSilentlyAndKeepsATweenStartedAfterIt) {
        TweenManager tweens;
        Vector<int> events;
        Tallies tallies;
        int resumed = 0;
        IntrusivePtr<TweenHandle> pending;
        IntrusivePtr<TweenHandle> after;
        const auto clearer = StartSilent(tweens, kQuarterSecond, [&] {
            pending = StartTracked(tweens, tallies);
            tweens.Clear();
            after = StartNumbered(tweens, events, 6, kOneSecond);
        });
        const auto running = StartTracked(tweens, tallies);
        const auto other = StartTracked(tweens, tallies);
        Task<void> waiter = AwaitHandle(running, resumed);
        waiter.Resume();

        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(clearer->GetState(), TweenState::Completed);
        for (const auto& handle : {running, other, pending}) {
            EXPECT_EQ(handle->GetState(), TweenState::Cancelled);
            EXPECT_TRUE(handle->IsFinished());
        }
        EXPECT_EQ(tallies.value, 3);  // the callables are destroyed, once each, and none runs
        EXPECT_EQ(tallies.complete, 3);
        EXPECT_EQ(tallies.cancel, 3);
        EXPECT_EQ(resumed, 0);
        EXPECT_EQ(after->GetState(), TweenState::Waiting);
        EXPECT_EQ(tweens.GetActiveCount(), 1u);

        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(events, (Vector<int>{6}));
    }

    TEST(TweenDenseStorageTest, ClearOutsideAWalkDropsEverythingAndTheManagerStartsAgain) {
        TweenManager tweens;
        Vector<int> events;
        Tallies tallies;
        int resumed = 0;
        const auto running = StartTracked(tweens, tallies);
        const auto other = StartTracked(tweens, tallies);
        Task<void> waiter = AwaitHandle(running, resumed);
        waiter.Resume();
        tweens.Advance(kQuarterSecond);

        tweens.Clear();

        EXPECT_EQ(running->GetState(), TweenState::Cancelled);
        EXPECT_EQ(running->GetProgress(), 0.25f);
        EXPECT_EQ(other->GetState(), TweenState::Cancelled);
        EXPECT_EQ(tallies.cancel, 2);
        EXPECT_EQ(tallies.complete, 2);
        EXPECT_EQ(resumed, 0);
        EXPECT_EQ(tweens.GetActiveCount(), 0u);

        const auto again = StartNumbered(tweens, events, 3, kQuarterSecond);
        running->Cancel();  // a handle from before the Clear must not reach a tween that took its id
        tweens.Advance(kQuarterSecond);

        EXPECT_EQ(events, (Vector<int>{3, kCompleteBase + 3}));
        EXPECT_EQ(again->GetState(), TweenState::Completed);
    }

    TEST(TweenDenseStorageTest, TombstonesBelowTheThresholdAreNotCountedAndTheCountsHoldAcrossACompaction) {
        TweenManager tweens;
        Vector<int> events;
        Vector<IntrusivePtr<TweenHandle>> handles;
        for (int number = 0; number < kFewEnded; ++number) {
            handles.push_back(StartNumbered(tweens, events, number, kQuarterSecond));
        }
        for (int number = kFewEnded; number < kFewEnded + kManyLive; ++number) {
            handles.push_back(StartNumbered(tweens, events, number, kLongSecond));
        }
        Timeline loop;
        loop.Append<float>(0.0f, 1.0f, Settings(kHalfSecond), {}).Yoyo().RepeatForever();
        loop.SetFrameClass(AnimationFrameClass::DecorativeLoop);
        const auto decoration = tweens.Play(std::move(loop));
        size_t live = static_cast<size_t>(kManyLive) + 1;

        tweens.Advance(kQuarterSecond);  // a few tombstones: the walk leaves them where they are
        EXPECT_EQ(tweens.GetActiveCount(), live);
        EXPECT_EQ(tweens.GetActiveCount(AnimationFrameClass::FiniteEffect), static_cast<size_t>(kManyLive));
        EXPECT_EQ(tweens.GetActiveCount(AnimationFrameClass::DecorativeLoop), 1u);
        EXPECT_EQ(tweens.GetActiveCount(AnimationFrameClass::Interactive), 0u);

        Vector<int> expected;
        for (int number = kFewEnded; number < kFewEnded + kManyLive; ++number) {
            expected.push_back(number);
        }
        events.clear();
        tweens.Advance(kQuarterSecond);
        EXPECT_EQ(events, expected);

        for (int number = kFewEnded; number < kFewEnded + kCancelledLive; ++number) {
            handles[static_cast<size_t>(number)]->Cancel();
        }
        expected.erase(expected.begin(), expected.begin() + kCancelledLive);
        live -= static_cast<size_t>(kCancelledLive);
        EXPECT_EQ(tweens.GetActiveCount(), live);

        for (int frame = 0; frame < 2; ++frame) {  // the first walk compacts, the second runs over the compacted list
            events.clear();
            tweens.Advance(kQuarterSecond);
            EXPECT_EQ(events, expected) << frame;
            EXPECT_EQ(tweens.GetActiveCount(), live) << frame;
            EXPECT_EQ(tweens.GetActiveCount(AnimationFrameClass::FiniteEffect), live - 1) << frame;
        }
        EXPECT_EQ(handles[kFewEnded + kCancelledLive]->GetProgress(), 0.25f);  // one of four seconds after four frames
        EXPECT_TRUE(decoration->GetState() == TweenState::Running);
    }

    TEST(TweenDenseStorageTest, AHandleThatOutlivesItsManagerReadsCancelledAndCancelsNothing) {
        Tallies tallies;
        int cancelCalls = 0;
        IntrusivePtr<TweenHandle> running;
        IntrusivePtr<TweenHandle> waiting;
        IntrusivePtr<TweenHandle> finished;
        IntrusivePtr<TweenHandle> finishedEarlier;
        {
            TweenManager tweens;
            finishedEarlier = StartSilent(tweens, kQuarterSecond);
            tweens.Advance(kQuarterSecond);
            running = StartTracked(tweens, tallies);
            waiting = tweens.Start<float>(0.0f, 1.0f, Settings(kOneSecond, kOneSecond), {}, {}, [&cancelCalls] { ++cancelCalls; });
            finished = StartSilent(tweens, kQuarterSecond);
            tweens.Advance(kQuarterSecond);
        }

        EXPECT_EQ(running->GetState(), TweenState::Cancelled);
        EXPECT_EQ(running->GetProgress(), 0.25f);
        EXPECT_EQ(waiting->GetState(), TweenState::Cancelled);
        EXPECT_EQ(finished->GetState(), TweenState::Completed);
        EXPECT_EQ(finishedEarlier->GetState(), TweenState::Completed);
        EXPECT_EQ(tallies.cancel, 1);  // the callable is destroyed with the manager, not run
        for (const auto& handle : {running, waiting, finished, finishedEarlier}) {
            handle->Cancel();
        }
        EXPECT_EQ(cancelCalls, 0);
        EXPECT_EQ(running->GetState(), TweenState::Cancelled);
        EXPECT_EQ(finished->GetState(), TweenState::Completed);
    }

    TEST(TweenDenseStorageTest, ASteadyAdvanceAllocatesNothing) {
        TweenManager tweens;
        float sink = 0.0f;
        Vector<IntrusivePtr<TweenHandle>> handles;
        for (int i = 0; i < kSteadyCount; ++i) {
            handles.push_back(tweens.Start<float>(0.0f, 1.0f, Settings(64.0f), [&sink](const float& value) { sink = value; }));
        }
        tweens.Advance(kSixteenthSecond);

        ExpectNoAllocations([&] {
            for (int frame = 0; frame < 4; ++frame) {
                tweens.Advance(kSixteenthSecond);
            }
        });
        EXPECT_GT(sink, 0.0f);
    }

    TEST(TweenDenseStorageTest, AnAdvanceInWhichATweenEndsAndRestartsItselfAllocatesNothing) {
        // A restart from `onComplete`: the next tween goes to `_pending` and is merged after the walk. Its handle is a pooled block that the pool hands out from its free list once it has grown, so
        // the warm-up covers it
        struct Restarter {
            TweenManager* tweens = nullptr;
            IntrusivePtr<TweenHandle> handle;
            int restarts = 0;

            void Start() {
                handle = tweens->Start<float>(0.0f, 1.0f, Settings(kSixteenthSecond), {}, [this] {
                    ++restarts;
                    Start();
                });
            }
        };
        TweenManager tweens;
        float sink = 0.0f;
        Vector<IntrusivePtr<TweenHandle>> handles;
        for (int i = 0; i < kSteadyCount; ++i) {
            handles.push_back(tweens.Start<float>(0.0f, 1.0f, Settings(64.0f), [&sink](const float& value) { sink = value; }));
        }
        Restarter restarter{.tweens = &tweens};
        restarter.Start();
        constexpr int kWarmFrames = 4;
        for (int frame = 0; frame < kWarmFrames; ++frame) {
            tweens.Advance(kSixteenthSecond);
        }
        const int restartsBefore = restarter.restarts;

        ExpectNoAllocations([&] {
            for (int frame = 0; frame < kWarmFrames; ++frame) {
                tweens.Advance(kSixteenthSecond);
            }
        });
        EXPECT_EQ(restarter.restarts, restartsBefore + kWarmFrames);
    }

}  // namespace BECore::Tests
