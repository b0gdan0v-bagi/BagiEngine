#include <BECore/Animation/Timeline.h>
#include <BECore/Math/Vec3.h>
#include <BECore/Math/Vec4.h>
#include <Common/DestroyTally.h>
#include <cmath>
#include <gtest/gtest.h>

namespace BECore::Tests {

    static_assert(!std::is_copy_constructible_v<Timeline>);
    static_assert(!std::is_copy_assignable_v<Timeline>);
    static_assert(std::is_nothrow_move_constructible_v<Timeline>);
    static_assert(std::is_nothrow_move_assignable_v<Timeline>);

    TEST(TimelineTest, CompileTimeChecks) {}

    namespace {

        /// The closure `MakeTweenApply` builds for a value of type @p T and an `onValue` that holds one pointer
        template <typename T>
        struct SetterProbe {
            static auto Make() {
                int* target = nullptr;
                auto onValue = [target](const T&) {};
                return [from = T{}, to = T{}, onValue](float) mutable {};
            }
            static constexpr bool kFits = kFunctionFitsInline<decltype(Make()), kTweenApplyBytes>;
        };

    }  // namespace

    static_assert(SetterProbe<float>::kFits);
    static_assert(SetterProbe<Vec2>::kFits);
    static_assert(SetterProbe<Vec3>::kFits);
    static_assert(SetterProbe<Vec4>::kFits);
    static_assert(SetterProbe<Color>::kFits);

    namespace {

        // Times and durations are powers of two, so the sums are exact in floating point
        constexpr float kNever = -1.0f;  // the value of a property nobody has written yet
        constexpr float kEighth = 0.125f;
        constexpr float kQuarter = 0.25f;
        constexpr float kHalf = 0.5f;
        constexpr float kThreeQuarters = 0.75f;
        constexpr float kOneSecond = 1.0f;  // past the end of most timelines here
        // The end values of the first, second and third clip of a timeline
        constexpr float kFirstTo = 10.0f;
        constexpr float kSecondTo = 20.0f;
        constexpr float kThirdTo = 30.0f;
        constexpr float kQuadInAtHalf = 0.25f;  // Ease(QuadIn, 0.5)
        constexpr int kRepeats = 3;
        constexpr int kMoreRepeats = 5;
        constexpr float kTenth = 0.1f;  // not a power of two: the sums round
        constexpr float kThirtyFiveHundredths = 0.35f;
        constexpr Color kTargetColor{200, 100, 50, 255};

        TweenSettings Settings(float duration, float delay = 0.0f, EaseType ease = EaseType::Linear) {
            return {duration, delay, ease};
        }

        /// A float property that records every value written to it
        struct Property {
            float value = kNever;
            int writes = 0;

            auto Setter() {
                return [this](const float& written) {
                    value = written;
                    ++writes;
                };
            }
        };

        /// What the callbacks saw, in order
        struct Calls {
            Vector<int> ids;

            MoveOnlyFunction<void()> Callback(int id) {
                return [this, id] { ids.push_back(id); };
            }
        };

        bool operator==(const Vector<int>& left, std::initializer_list<int> right) {
            return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
        }

    }  // namespace

    TEST(TimelineTest, AppendIsASequence) {
        Property a;
        Property b;
        Property c;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kQuarter), a.Setter())
            .Append<float>(0.0f, kSecondTo, Settings(kQuarter), b.Setter())
            .Append<float>(0.0f, kThirdTo, Settings(kQuarter), c.Setter());
        EXPECT_EQ(timeline.GetDuration(), kThreeQuarters);

        timeline.Evaluate(kEighth);
        EXPECT_EQ(a.value, kFirstTo / 2);
        EXPECT_EQ(b.writes, 0);
        EXPECT_EQ(c.writes, 0);

        // The end of the first clip is the start of the second: the first is final, the second is entered at its from
        timeline.Evaluate(kQuarter);
        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_EQ(b.value, 0.0f);
        EXPECT_EQ(c.writes, 0);

        timeline.Evaluate(kHalf);
        EXPECT_EQ(b.value, kSecondTo);
        EXPECT_EQ(c.value, 0.0f);

        timeline.Evaluate(kThreeQuarters);
        EXPECT_EQ(c.value, kThirdTo);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, JoinStartsWithThePreviousClipAndTheDurationIsTheLongerOne) {
        Property a;
        Property b;
        Property c;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kHalf), a.Setter()).Join<float>(0.0f, kSecondTo, Settings(kQuarter), b.Setter()).Append<float>(0.0f, kThirdTo, Settings(kQuarter), c.Setter());
        EXPECT_EQ(timeline.GetDuration(), kThreeQuarters);

        timeline.Evaluate(kQuarter);
        EXPECT_EQ(a.value, kFirstTo / 2);
        EXPECT_EQ(b.value, kSecondTo);
        EXPECT_EQ(c.writes, 0);

        // `Append` after a shorter joined clip waits for the longer one: the end is the latest end
        timeline.Evaluate(kHalf);
        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_EQ(c.value, 0.0f);
    }

    TEST(TimelineTest, JoinOnAnEmptyTimelineStartsAtZero) {
        Property a;
        Timeline timeline;
        timeline.Join<float>(0.0f, kFirstTo, Settings(kQuarter), a.Setter());
        timeline.Evaluate(kEighth);
        EXPECT_EQ(a.value, kFirstTo / 2);
    }

    TEST(TimelineTest, InsertPlacesAClipAtAnAbsoluteTimeAndExtendsTheDuration) {
        Property a;
        Property b;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kQuarter), a.Setter()).Insert<float>(kHalf, 0.0f, kSecondTo, Settings(kQuarter), b.Setter());
        EXPECT_EQ(timeline.GetDuration(), kThreeQuarters);

        timeline.Evaluate(kQuarter);
        EXPECT_EQ(b.writes, 0);
        timeline.Evaluate(kHalf + kEighth);
        EXPECT_EQ(b.value, kSecondTo / 2);
    }

    TEST(TimelineTest, InsertBetweenTwoStartsKeepsTheStartOrder) {
        Property a;
        Property b;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kQuarter), a.Setter()).Append<float>(0.0f, kSecondTo, Settings(kQuarter), b.Setter());
        Calls calls;
        // At 0.125, between the two clips' starts; the callback is visited after the first clip and before the second
        timeline.InsertCallback(kEighth, calls.Callback(1));
        timeline.Evaluate(kThreeQuarters);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1}));
        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_EQ(b.value, kSecondTo);
    }

    TEST(TimelineTest, AppendDelayIsASpacerThatCallsNothing) {
        Property a;
        Property b;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kQuarter), a.Setter()).AppendDelay(kQuarter).Append<float>(0.0f, kSecondTo, Settings(kQuarter), b.Setter());
        EXPECT_EQ(timeline.GetDuration(), kThreeQuarters);

        timeline.Evaluate(kQuarter);
        const int writes = a.writes;
        timeline.Evaluate(kQuarter + kEighth);  // inside the spacer
        EXPECT_EQ(a.writes, writes);
        EXPECT_EQ(b.writes, 0);

        timeline.Evaluate(kHalf + kEighth);
        EXPECT_EQ(b.value, kSecondTo / 2);
    }

    TEST(TimelineTest, CallbacksFireOnceAtTheirTimeInOrderWithTheClips) {
        Calls calls;
        Property a;
        Timeline timeline;
        timeline.AppendCallback(calls.Callback(1)).Append<float>(0.0f, kFirstTo, Settings(kQuarter), [&](const float& value) {
            a.value = value;
            calls.ids.push_back(2);
        });
        timeline.AppendCallback(calls.Callback(3));
        EXPECT_EQ(timeline.GetDuration(), kQuarter);

        timeline.Evaluate(kEighth);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2}));
        timeline.Evaluate(kQuarter);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2, 2, 3}));
        timeline.Evaluate(kOneSecond);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2, 2, 3}));
    }

    TEST(TimelineTest, AJumpOverSeveralCallbacksFiresThemAllInOrder) {
        Calls calls;
        Timeline timeline;
        timeline.InsertCallback(kHalf, calls.Callback(3)).InsertCallback(kQuarter, calls.Callback(2)).InsertCallback(0.0f, calls.Callback(1));
        timeline.Evaluate(kOneSecond);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2, 3}));
    }

    TEST(TimelineTest, SetterCallsFollowTheEaseOfTheLocalProgressAndTheLastIsExactlyTo) {
        Property a;
        const float to = 0.1f;
        Timeline timeline;
        timeline.Append<float>(0.0f, to, Settings(kOneSecond, 0.0f, EaseType::QuadIn), a.Setter());

        timeline.Evaluate(kHalf);
        EXPECT_EQ(a.value, to * kQuadInAtHalf);
        timeline.Evaluate(kOneSecond);
        EXPECT_EQ(a.value, to);
        EXPECT_EQ(a.writes, 2);
    }

    TEST(TimelineTest, AJumpOverAWholeClipStillGivesOneFinalSetter) {
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kQuarter), a.Setter()).AppendDelay(kQuarter);
        timeline.Evaluate(kHalf);
        EXPECT_EQ(a.writes, 1);
        EXPECT_EQ(a.value, kFirstTo);
    }

    TEST(TimelineTest, AJumpOverTheWholeTimelineLeavesTheValueOfTheLastWriter) {
        Property a;
        Property b;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kQuarter), a.Setter())
            .Append<float>(0.0f, kSecondTo, Settings(kQuarter), a.Setter())
            .Join<float>(0.0f, kThirdTo, Settings(kQuarter), b.Setter());
        timeline.Evaluate(kOneSecond);
        EXPECT_EQ(a.value, kSecondTo);
        EXPECT_EQ(a.writes, 2);
        EXPECT_EQ(b.value, kThirdTo);
    }

    TEST(TimelineTest, EvaluatingTheDurationFinishesEverythingOnceAndLaterCallsChangeNothing) {
        Property a;
        Calls calls;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kQuarter), a.Setter()).AppendCallback(calls.Callback(1));
        EXPECT_FALSE(timeline.IsFinished());

        timeline.Evaluate(timeline.GetDuration());
        EXPECT_TRUE(timeline.IsFinished());
        EXPECT_EQ(a.writes, 1);
        EXPECT_EQ(calls.ids.size(), 1u);

        timeline.Evaluate(timeline.GetDuration());
        timeline.Evaluate(kOneSecond);
        EXPECT_EQ(a.writes, 1);
        EXPECT_EQ(calls.ids.size(), 1u);
    }

    TEST(TimelineTest, Vec2AndColorClips) {
        Vec2 position;
        Color color;
        Timeline timeline;
        timeline.Append<Vec2>({0.0f, 0.0f}, {kFirstTo, kSecondTo}, Settings(kQuarter), [&](const Vec2& value) { position = value; });
        timeline.Join<Color>({0, 0, 0, 0}, kTargetColor, Settings(kQuarter), [&](const Color& value) { color = value; });

        timeline.Evaluate(kEighth);
        EXPECT_EQ(position, Vec2(kFirstTo / 2, kSecondTo / 2));
        EXPECT_EQ(color.r, kTargetColor.r / 2);
        EXPECT_EQ(color.g, kTargetColor.g / 2);

        timeline.Evaluate(kQuarter);
        EXPECT_EQ(position, Vec2(kFirstTo, kSecondTo));
        EXPECT_EQ(color.r, kTargetColor.r);
        EXPECT_EQ(color.a, kTargetColor.a);
    }

    TEST(TimelineTest, AClipDelayLengthensItsSlot) {
        Property a;
        Property b;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kQuarter, kQuarter), a.Setter()).Append<float>(0.0f, kSecondTo, Settings(kQuarter), b.Setter());
        EXPECT_EQ(timeline.GetDuration(), kThreeQuarters);

        timeline.Evaluate(kEighth);  // inside the delay: no setter call
        EXPECT_EQ(a.writes, 0);
        timeline.Evaluate(kQuarter + kEighth);
        EXPECT_EQ(a.value, kFirstTo / 2);
        EXPECT_EQ(b.writes, 0);
        timeline.Evaluate(kHalf);
        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_EQ(b.value, 0.0f);
    }

    TEST(TimelineTest, ASubTimelineIsAClipWithItsOwnLocalTime) {
        Property inner;
        Property outer;
        Calls calls;
        Timeline sub;
        sub.AppendCallback(calls.Callback(1)).Append<float>(0.0f, kFirstTo, Settings(kQuarter), inner.Setter()).AppendCallback(calls.Callback(2));

        Timeline timeline;
        timeline.Append<float>(0.0f, kSecondTo, Settings(kQuarter), outer.Setter()).Append(std::move(sub));
        EXPECT_EQ(timeline.GetDuration(), kHalf);

        timeline.Evaluate(kQuarter - kEighth);
        EXPECT_EQ(calls.ids.size(), 0u);
        timeline.Evaluate(kQuarter + kEighth);  // the sub-timeline's time is 0.125
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1}));
        EXPECT_EQ(inner.value, kFirstTo / 2);
        timeline.Evaluate(kHalf);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2}));
        EXPECT_EQ(inner.value, kFirstTo);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, ARepeatingSubTimelineCountsItsRepeatsInTheParentDuration) {
        Calls calls;
        Timeline sub;
        sub.AppendCallback(calls.Callback(1)).AppendDelay(kQuarter).Repeat(2);
        Timeline timeline;
        timeline.Insert(kQuarter, std::move(sub));
        EXPECT_EQ(timeline.GetDuration(), kThreeQuarters);
        timeline.Evaluate(kThreeQuarters);
        // The callback sits at the start of the sub-timeline, so each of its two iterations fires it once
        EXPECT_EQ(calls.ids.size(), 2u);
    }

    TEST(TimelineTest, RepeatRunsTheIterationThreeTimes) {
        Property a;
        Calls calls;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kQuarter), a.Setter()).AppendCallback(calls.Callback(1)).Repeat(kRepeats);
        EXPECT_EQ(timeline.GetDuration(), kThreeQuarters);

        timeline.Evaluate(kQuarter);
        EXPECT_EQ(calls.ids.size(), 1u);
        // The second iteration starts from `from` again, with the left-over time
        timeline.Evaluate(kQuarter + kEighth);
        EXPECT_EQ(a.value, kFirstTo / 2);
        timeline.Evaluate(kHalf);
        EXPECT_EQ(calls.ids.size(), 2u);
        EXPECT_FALSE(timeline.IsFinished());
        timeline.Evaluate(kThreeQuarters);
        EXPECT_EQ(calls.ids.size(), 3u);
        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, ABigJumpCrossesIterationsAndLosesNoCallback) {
        Property a;
        Calls calls;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kQuarter), a.Setter()).AppendCallback(calls.Callback(1)).Repeat(kRepeats);
        timeline.Evaluate(kOneSecond);
        EXPECT_EQ(calls.ids.size(), 3u);
        EXPECT_EQ(a.writes, kRepeats);
        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_TRUE(timeline.IsFinished());

        // A jump that ends inside the second iteration
        Calls second;
        Property b;
        Timeline other;
        other.Append<float>(0.0f, kFirstTo, Settings(kQuarter), b.Setter()).AppendCallback(second.Callback(1)).Repeat(kRepeats);
        other.Evaluate(kQuarter + kEighth);
        EXPECT_EQ(second.ids.size(), 1u);
        EXPECT_EQ(b.value, kFirstTo / 2);
    }

    TEST(TimelineTest, ARepeatBelowOneIsOne) {
        for (const int count : {0, -1, -100}) {
            Calls calls;
            Timeline timeline;
            timeline.AppendDelay(kQuarter).AppendCallback(calls.Callback(1)).Repeat(count);
            EXPECT_EQ(timeline.GetDuration(), kQuarter) << count;
            timeline.Evaluate(kOneSecond);
            EXPECT_EQ(calls.ids.size(), 1u) << count;
        }
    }

    TEST(TimelineTest, AZeroLengthTimelineIsDoneByTheFirstCallAndDoesNotLoop) {
        Calls calls;
        Timeline timeline;
        timeline.AppendCallback(calls.Callback(1));
        EXPECT_EQ(timeline.GetDuration(), 0.0f);
        timeline.Evaluate(0.0f);
        EXPECT_TRUE(timeline.IsFinished());
        EXPECT_EQ(calls.ids.size(), 1u);

        Calls looped;
        Timeline repeated;
        repeated.AppendCallback(looped.Callback(1)).Repeat(kMoreRepeats);
        repeated.Evaluate(kOneSecond);
        EXPECT_TRUE(repeated.IsFinished());
        EXPECT_EQ(looped.ids.size(), 1u);

        Timeline empty;
        empty.Evaluate(kEighth);
        EXPECT_TRUE(empty.IsFinished());
    }

    TEST(TimelineTest, AZeroLengthClipGetsItsFinalSetterAtItsStart) {
        Property a;
        Timeline timeline;
        timeline.AppendDelay(kQuarter).Append<float>(0.0f, kFirstTo, Settings(0.0f), a.Setter());
        timeline.Evaluate(kEighth);
        EXPECT_EQ(a.writes, 0);
        timeline.Evaluate(kQuarter);
        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_EQ(a.writes, 1);
    }

    TEST(TimelineTest, ResetMakesTheTimelineRunAgainFromTheStart) {
        Property a;
        Calls calls;
        Timeline sub;
        sub.AppendCallback(calls.Callback(2));
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kQuarter), a.Setter()).AppendCallback(calls.Callback(1)).Append(std::move(sub));
        timeline.Evaluate(kOneSecond);
        EXPECT_TRUE(timeline.IsFinished());
        EXPECT_EQ(calls.ids.size(), 2u);

        timeline.Reset();
        EXPECT_FALSE(timeline.IsFinished());
        timeline.Evaluate(kEighth);
        EXPECT_EQ(a.value, kFirstTo / 2);
        timeline.Evaluate(kOneSecond);
        EXPECT_EQ(calls.ids.size(), 4u);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2, 1, 2}));
    }

    TEST(TimelineTest, EqualStartTimesKeepTheInsertionOrder) {
        Calls calls;
        Timeline timeline;
        timeline.InsertCallback(kQuarter, calls.Callback(1)).InsertCallback(kQuarter, calls.Callback(2)).InsertCallback(0.0f, calls.Callback(0)).InsertCallback(kQuarter, calls.Callback(3));
        timeline.Evaluate(kOneSecond);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({0, 1, 2, 3}));
    }

    TEST(TimelineTest, BadTimesCountAsZero) {
        constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
        constexpr float kInfinity = std::numeric_limits<float>::infinity();
        Property a;
        Calls calls;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kNan, kInfinity), a.Setter())
            .Insert<float>(-kOneSecond, 0.0f, kSecondTo, Settings(-kOneSecond), a.Setter())
            .InsertCallback(kNan, calls.Callback(1))
            .AppendDelay(kNan);
        EXPECT_EQ(timeline.GetDuration(), 0.0f);
        timeline.Evaluate(0.0f);
        EXPECT_TRUE(timeline.IsFinished());
        EXPECT_EQ(a.value, kSecondTo);
        EXPECT_EQ(calls.ids.size(), 1u);
    }

    TEST(TimelineTest, ANanTimeIsIgnoredAndANegativeOneIsZero) {
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kQuarter), a.Setter());
        timeline.Evaluate(std::numeric_limits<float>::quiet_NaN());
        EXPECT_EQ(a.writes, 0);
        timeline.Evaluate(-kOneSecond);
        EXPECT_EQ(a.value, 0.0f);
        timeline.Evaluate(std::numeric_limits<float>::infinity());
        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, AMovedTimelineKeepsItsClips) {
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kQuarter), a.Setter());
        Timeline moved = std::move(timeline);
        EXPECT_EQ(moved.GetDuration(), kQuarter);
        moved.Evaluate(kQuarter);
        EXPECT_EQ(a.value, kFirstTo);
    }

    TEST(TimelineTest, ARepeatedTimelineThatIsNotAPowerOfTwoEndsExactlyAtItsDuration) {
        // 0.35 * 3 rounds: the last iteration's local time falls a hair under the iteration length
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kThirtyFiveHundredths), a.Setter()).Repeat(kRepeats);
        timeline.Evaluate(timeline.GetDuration());
        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, ARepeatedTimelineFiresEveryCallbackAtItsDuration) {
        Calls calls;
        Timeline timeline;
        timeline.AppendDelay(kTenth).AppendCallback(calls.Callback(1)).Repeat(kMoreRepeats);
        timeline.Evaluate(timeline.GetDuration());
        EXPECT_EQ(calls.ids.size(), static_cast<size_t>(kMoreRepeats));
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, ARepeatedSubTimelineThatIsNotAPowerOfTwoFinishesItsParent) {
        Calls calls;
        Property a;
        Timeline sub;
        sub.Append<float>(0.0f, kFirstTo, Settings(kThirtyFiveHundredths), a.Setter()).AppendCallback(calls.Callback(1)).Repeat(kRepeats);
        Timeline timeline;
        timeline.AppendDelay(kTenth).Append(std::move(sub));
        timeline.Evaluate(timeline.GetDuration());
        EXPECT_EQ(calls.ids.size(), static_cast<size_t>(kRepeats));
        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_TRUE(timeline.IsFinished());

        Calls fives;
        Timeline sub2;
        sub2.AppendDelay(kTenth).AppendCallback(fives.Callback(1)).Repeat(kMoreRepeats);
        Timeline other;
        other.Append(std::move(sub2));
        other.Evaluate(other.GetDuration());
        EXPECT_EQ(fives.ids.size(), static_cast<size_t>(kMoreRepeats));
        EXPECT_TRUE(other.IsFinished());
    }

    TEST(TimelineTest, AJoinAfterASpacerStartsWhereTheSpacerStarts) {
        Property a;
        Timeline timeline;
        timeline.AppendDelay(kHalf).Join<float>(0.0f, kFirstTo, Settings(kHalf), a.Setter());
        EXPECT_EQ(timeline.GetDuration(), kHalf);
        timeline.Evaluate(kQuarter);
        EXPECT_EQ(a.value, kFirstTo / 2);
        timeline.Evaluate(kHalf);
        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, ATypedClipHoldsAMoveOnlyOnValueAndDestroysItOnce) {
        int destroyed = 0;
        float value = kNever;
        {
            Timeline timeline;
            timeline.Append<float>(0.0f, kFirstTo, Settings(kQuarter), [token = DestroyTally(destroyed), &value](const float& written) { value = written; });
            Timeline moved = std::move(timeline);
            moved.Evaluate(kQuarter);
            EXPECT_EQ(value, kFirstTo);
            EXPECT_EQ(destroyed, 0);
        }
        EXPECT_EQ(destroyed, 1);
    }

    TEST(TimelineTest, AClipTakesNoSetterAndTheTypeIsDeducedFromTheValues) {
        Property a;
        Timeline timeline;
        timeline.Append(0.0f, kFirstTo, Settings(kQuarter), a.Setter()).Join<float>(0.0f, kSecondTo, Settings(kQuarter), {}).Insert<float>(kQuarter, 0.0f, kSecondTo, Settings(kQuarter), nullptr);
        EXPECT_EQ(timeline.GetDuration(), kHalf);

        timeline.Evaluate(kOneSecond);

        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_EQ(a.writes, 1);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, TheStopCheckOfTheRootStopsASubTimelineTwoLevelsDownAtTheClipBoundary) {
        bool stop = false;
        Calls calls;
        Property inner;
        Timeline innermost;
        innermost.AppendCallback([&] {
            stop = true;
            calls.ids.push_back(1);
        });
        innermost.Append<float>(0.0f, kFirstTo, Settings(kQuarter), inner.Setter());
        Timeline middle;
        middle.Append(std::move(innermost)).AppendCallback(calls.Callback(2));
        Timeline root;
        root.Append(std::move(middle)).AppendCallback(calls.Callback(3));
        root.SetStopCheck([&stop] { return stop; });

        root.Evaluate(kOneSecond);

        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1}));
        EXPECT_EQ(inner.writes, 0);
        EXPECT_FALSE(root.IsFinished());
    }

    TEST(TimelineTest, ASubTimelineWithACheckOfItsOwnIsGovernedByTheRoot) {
        Property inner;
        Calls calls;
        Timeline sub;
        sub.SetStopCheck([] { return true; });  // would stop the sub at once if it were the one evaluated
        sub.Append<float>(0.0f, kFirstTo, Settings(kQuarter), inner.Setter());
        Timeline root;
        root.Append(std::move(sub)).AppendCallback(calls.Callback(1));

        root.Evaluate(kOneSecond);

        EXPECT_EQ(inner.value, kFirstTo);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1}));
        EXPECT_TRUE(root.IsFinished());
    }

    namespace {

        constexpr float kSixteenth = 0.0625f;
        constexpr float kThreeEighths = 0.375f;
        constexpr float kQuadInAtThreeQuarters = 0.5625f;  // Ease(QuadIn, 3/4)
        constexpr float kQuadInAtQuarter = 0.0625f;        // Ease(QuadIn, 1/4)
        constexpr float kTwo = 2.0f;
        constexpr float kTwentyFive = 25.0f;
        constexpr int kFour = 4;
        constexpr int kSixteen = 16;

        /// The writes of several properties in one sequence: (property id, value)
        struct Trace {
            Vector<Pair<int, float>> writes;

            auto Setter(int id) {
                return [this, id](const float& written) { writes.push_back({id, written}); };
            }
        };

        /// Evaluates a timeline of `Repeat(2).Yoyo()` over a forward leg and a backward one of @p leg seconds on a 1/16 s grid, and checks that
        /// the writes of the backward leg are those of the forward one in reverse order
        void ExpectTheBackwardLegReversesTheForwardOne(Timeline& timeline, const Trace& trace, float leg) {
            const int steps = static_cast<int>(leg / kSixteenth);
            for (int step = 0; step < steps; ++step) {
                timeline.Evaluate(static_cast<float>(step) * kSixteenth);
            }
            const size_t forwardCount = trace.writes.size();
            timeline.Evaluate(leg);  // the end of one leg and the start of the other, in one call
            const size_t boundary = trace.writes.size();
            for (int step = 1; step <= steps; ++step) {
                timeline.Evaluate(leg + static_cast<float>(step) * kSixteenth);
            }
            EXPECT_TRUE(timeline.IsFinished());
            ASSERT_EQ(trace.writes.size() - boundary, forwardCount);
            for (size_t index = 0; index < forwardCount; ++index) {
                const Pair<int, float>& forward = trace.writes[forwardCount - 1 - index];
                const Pair<int, float>& backward = trace.writes[boundary + index];
                EXPECT_EQ(backward.first, forward.first) << "write " << index;
                EXPECT_EQ(backward.second, forward.second) << "write " << index;
            }
        }

    }  // namespace

    TEST(TimelineTest, AYoyoThatIsOffChangesNothing) {
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kHalf), a.Setter()).Repeat(2).Yoyo(false);
        timeline.Evaluate(kThreeQuarters);
        EXPECT_EQ(a.value, kFirstTo / 2);  // the second iteration runs forward again
        timeline.Evaluate(kOneSecond);
        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, ABackwardIterationWalksTheSameCurveBackAndEndsExactlyOnFrom) {
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kHalf, 0.0f, EaseType::QuadIn), a.Setter()).Repeat(2).Yoyo();
        EXPECT_EQ(timeline.GetDuration(), kOneSecond);

        timeline.Evaluate(kQuarter);
        EXPECT_EQ(a.value, kFirstTo * kQuadInAtHalf);

        timeline.Evaluate(kHalf + kEighth);  // the backward leg at playback progress 1/4
        EXPECT_EQ(a.value, kFirstTo * kQuadInAtThreeQuarters);
        timeline.Evaluate(kHalf + kQuarter);
        EXPECT_EQ(a.value, kFirstTo * kQuadInAtHalf);
        timeline.Evaluate(kHalf + kThreeEighths);
        EXPECT_EQ(a.value, kFirstTo * kQuadInAtQuarter);
        EXPECT_FALSE(timeline.IsFinished());

        timeline.Evaluate(kOneSecond);
        EXPECT_EQ(a.value, 0.0f);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, ABackOutComesBackThroughItsBulgeInReverseOrder) {
        Property a;
        Vector<float> forward;
        Vector<float> backward;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kHalf, 0.0f, EaseType::BackOut), a.Setter()).Repeat(2).Yoyo();
        for (int step = 0; step < 8; ++step) {
            timeline.Evaluate(static_cast<float>(step) * kSixteenth);
            forward.push_back(a.value);
        }
        timeline.Evaluate(kHalf);
        for (int step = 1; step <= 8; ++step) {
            timeline.Evaluate(kHalf + static_cast<float>(step) * kSixteenth);
            backward.push_back(a.value);
        }

        EXPECT_GT(*eastl::max_element(forward.begin(), forward.end()), kFirstTo);  // the bulge
        ASSERT_EQ(backward.size(), forward.size());
        for (size_t index = 0; index < forward.size(); ++index) {
            EXPECT_EQ(backward[index], forward[forward.size() - 1 - index]) << index;
        }
        EXPECT_EQ(a.value, 0.0f);
    }

    TEST(TimelineTest, ThreeYoyoIterationsAreForwardBackwardForwardAndEndExactlyOnTo) {
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kHalf), a.Setter()).Repeat(kRepeats).Yoyo();
        EXPECT_EQ(timeline.GetDuration(), kOneSecond + kHalf);

        timeline.Evaluate(kHalf + kQuarter);
        EXPECT_EQ(a.value, kFirstTo / 2);  // backward at progress 1/2
        timeline.Evaluate(kOneSecond + kEighth);
        EXPECT_EQ(a.value, kFirstTo / 4);  // forward again, at progress 1/4
        timeline.Evaluate(kOneSecond + kHalf);
        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_TRUE(timeline.IsFinished());

        Property b;
        Timeline jump;
        jump.Append<float>(0.0f, kFirstTo, Settings(kHalf), b.Setter()).Repeat(kRepeats).Yoyo();
        jump.Evaluate(jump.GetDuration());
        EXPECT_EQ(b.value, kFirstTo);
        EXPECT_TRUE(jump.IsFinished());
    }

    TEST(TimelineTest, TwoClipsOnOnePropertyEndTheBackwardLegOnTheFirstClipsFrom) {
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kHalf), a.Setter()).Append<float>(kSecondTo, kThirdTo, Settings(kHalf), a.Setter()).Repeat(2).Yoyo();

        timeline.Evaluate(kOneSecond);
        EXPECT_EQ(a.value, kThirdTo);  // forward: the later clip wins

        timeline.Evaluate(kOneSecond + kQuarter);  // backward: the later clip is entered first, at its `to`
        EXPECT_EQ(a.value, kTwentyFive);
        timeline.Evaluate(kOneSecond + kHalf);
        EXPECT_EQ(a.value, kFirstTo);  // the first clip is entered at its `to`
        timeline.Evaluate(kOneSecond + kThreeQuarters);
        EXPECT_EQ(a.value, kFirstTo / 2);
        timeline.Evaluate(kOneSecond + kOneSecond);
        EXPECT_EQ(a.value, 0.0f);
    }

    TEST(TimelineTest, TheDelayOfAClipSitsAtTheEndOfItsBackwardSlot) {
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kHalf, kQuarter), a.Setter()).Repeat(2).Yoyo();
        EXPECT_EQ(timeline.GetDuration(), kThreeQuarters * 2);

        timeline.Evaluate(kEighth);
        EXPECT_EQ(a.writes, 0);  // the forward delay
        timeline.Evaluate(kHalf);
        EXPECT_EQ(a.value, kFirstTo / 2);

        timeline.Evaluate(kThreeQuarters + kQuarter);  // the backward leg: the active part comes first
        EXPECT_EQ(a.value, kFirstTo / 2);
        timeline.Evaluate(kThreeQuarters + kHalf);
        EXPECT_EQ(a.value, 0.0f);  // exactly `from` at the end of the active part
        const int writes = a.writes;
        timeline.Evaluate(kThreeQuarters + kThreeQuarters - kEighth);
        EXPECT_EQ(a.writes, writes);  // nothing during the delay
        timeline.Evaluate(kThreeQuarters * 2);
        EXPECT_EQ(a.writes, writes);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, AZeroLengthClipWritesToForwardAndFromBackward) {
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(0.0f), a.Setter()).AppendDelay(kHalf).Repeat(2).Yoyo();

        timeline.Evaluate(0.0f);
        EXPECT_EQ(a.value, kFirstTo);
        timeline.Evaluate(kThreeQuarters);
        EXPECT_EQ(a.value, kFirstTo);  // the backward leg enters the clip at its end
        EXPECT_FALSE(timeline.IsFinished());
        timeline.Evaluate(kOneSecond);
        EXPECT_EQ(a.value, 0.0f);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, ACallbackFiresOncePerIterationAtTheMirroredTimeAndInTheMirroredOrder) {
        Calls calls;
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kOneSecond), a.Setter())
            .InsertCallback(kQuarter, calls.Callback(1))
            .InsertCallback(kQuarter, calls.Callback(2))
            .InsertCallback(kThreeQuarters, calls.Callback(3))
            .Repeat(2)
            .Yoyo();

        timeline.Evaluate(kOneSecond);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2, 3}));
        timeline.Evaluate(kOneSecond + kEighth);
        EXPECT_EQ(calls.ids.size(), 3u);
        timeline.Evaluate(kOneSecond + kQuarter);  // 1 - 3/4
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2, 3, 3}));
        timeline.Evaluate(kOneSecond + kThreeQuarters);  // 1 - 1/4: the equal times in reverse insertion order
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2, 3, 3, 2, 1}));
        timeline.Evaluate(kTwo);
        EXPECT_EQ(calls.ids.size(), 6u);
    }

    TEST(TimelineTest, OneEvaluateAcrossForwardBackwardAndForwardLosesNoCallback) {
        Calls calls;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kOneSecond), {}).InsertCallback(kQuarter, calls.Callback(1)).InsertCallback(kThreeQuarters, calls.Callback(2)).Repeat(kRepeats).Yoyo();
        timeline.Evaluate(timeline.GetDuration());
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2, 2, 1, 1, 2}));
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, ABigJumpOverAYoyoGivesWhatManySmallStepsGive) {
        const auto build = [](Property& property, Calls& calls) {
            Timeline timeline;
            timeline.Append<float>(0.0f, kFirstTo, Settings(kHalf, 0.0f, EaseType::QuadIn), property.Setter())
                .AppendCallback(calls.Callback(1))
                .InsertCallback(kEighth, calls.Callback(2))
                .Repeat(kMoreRepeats)
                .Yoyo();
            return timeline;
        };
        Property jumped;
        Calls jumpedCalls;
        Timeline jump = build(jumped, jumpedCalls);
        jump.Evaluate(jump.GetDuration());

        Property stepped;
        Calls steppedCalls;
        Timeline steps = build(stepped, steppedCalls);
        constexpr float kStep = 1.0f / 64.0f;
        for (float time = 0.0f; !steps.IsFinished(); time += kStep) {
            steps.Evaluate(time);
        }

        EXPECT_EQ(jumped.value, stepped.value);
        EXPECT_EQ(jumped.value, kFirstTo);  // five iterations: forward, backward, forward, backward, forward
        EXPECT_EQ(jumpedCalls.ids.size(), static_cast<size_t>(kMoreRepeats) * 2);
        EXPECT_TRUE(jumpedCalls.ids == steppedCalls.ids);
    }

    TEST(TimelineTest, ASubTimelineInABackwardIterationIsPlayedReversedAsAWhole) {
        Trace trace;
        Timeline sub;
        sub.Append<float>(0.0f, kFirstTo, Settings(kQuarter, 0.0f, EaseType::QuadIn), trace.Setter(1)).Append<float>(0.0f, kSecondTo, Settings(kQuarter), trace.Setter(2));
        Timeline timeline;
        timeline.Append(std::move(sub)).Repeat(2).Yoyo();
        EXPECT_EQ(timeline.GetDuration(), kOneSecond);

        ExpectTheBackwardLegReversesTheForwardOne(timeline, trace, kHalf);
    }

    TEST(TimelineTest, ASubTimelineWithItsOwnRepeatIsReversedAsAWhole) {
        Trace trace;
        Timeline sub;
        sub.Append<float>(0.0f, kFirstTo, Settings(kQuarter, 0.0f, EaseType::QuadIn), trace.Setter(1)).Append<float>(0.0f, kSecondTo, Settings(kQuarter), trace.Setter(2)).Repeat(2);
        Timeline timeline;
        timeline.Append(std::move(sub)).Repeat(2).Yoyo();
        EXPECT_EQ(timeline.GetDuration(), kTwo);

        ExpectTheBackwardLegReversesTheForwardOne(timeline, trace, kOneSecond);
    }

    TEST(TimelineTest, ASubTimelineWithItsOwnYoyoIsReversedAsAWhole) {
        Trace trace;
        Timeline sub;
        sub.Append<float>(0.0f, kFirstTo, Settings(kQuarter, 0.0f, EaseType::QuadIn), trace.Setter(1)).Append<float>(0.0f, kSecondTo, Settings(kQuarter), trace.Setter(2)).Repeat(2).Yoyo();
        Timeline timeline;
        timeline.Append(std::move(sub)).Repeat(2).Yoyo();

        ExpectTheBackwardLegReversesTheForwardOne(timeline, trace, kOneSecond);
    }

    TEST(TimelineTest, ASingleIterationIsNotChangedByAYoyo) {
        Trace plain;
        Trace yoyo;
        const auto run = [](Trace& trace, bool enabled) {
            Timeline timeline;
            timeline.Append<float>(0.0f, kFirstTo, Settings(kHalf, 0.0f, EaseType::QuadIn), trace.Setter(1)).AppendCallback({}).Repeat(1).Yoyo(enabled);
            for (int step = 0; step <= kSixteen; ++step) {
                timeline.Evaluate(static_cast<float>(step) * kSixteenth);
            }
            EXPECT_TRUE(timeline.IsFinished());
        };
        run(plain, false);
        run(yoyo, true);
        ASSERT_EQ(plain.writes.size(), yoyo.writes.size());
        for (size_t index = 0; index < plain.writes.size(); ++index) {
            EXPECT_EQ(plain.writes[index].second, yoyo.writes[index].second) << index;
        }
    }

    TEST(TimelineTest, ResetAfterAFinishedYoyoRunsForwardAgain) {
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kHalf), a.Setter()).Repeat(2).Yoyo();
        timeline.Evaluate(kOneSecond);
        EXPECT_TRUE(timeline.IsFinished());
        EXPECT_EQ(a.value, 0.0f);

        timeline.Reset();
        timeline.Evaluate(kQuarter);
        EXPECT_EQ(a.value, kFirstTo / 2);
        timeline.Evaluate(kHalf + kQuarter);
        EXPECT_EQ(a.value, kFirstTo / 2);
        timeline.Evaluate(kOneSecond);
        EXPECT_EQ(a.value, 0.0f);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, TheStopCheckStopsABackwardIterationAtTheClipBoundary) {
        bool stop = false;
        int secondCalls = 0;
        Calls calls;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kOneSecond), {})
            .InsertCallback(kQuarter, calls.Callback(1))
            .InsertCallback(kThreeQuarters,
                            [&] {
                                calls.ids.push_back(2);
                                stop = ++secondCalls == 2;  // the backward visit of it
                            })
            .Repeat(2)
            .Yoyo();
        timeline.SetStopCheck([&stop] { return stop; });

        timeline.Evaluate(kTwo);

        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2, 2}));  // the backward leg enters 2 first: 1 is never called
        EXPECT_FALSE(timeline.IsFinished());
    }

    TEST(TimelineTest, AYoyoThatIsNotAPowerOfTwoEndsExactlyOnFromAtItsDuration) {
        Property a;
        Calls calls;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kThirtyFiveHundredths), a.Setter()).AppendCallback(calls.Callback(1)).Repeat(kFour).Yoyo();
        timeline.Evaluate(timeline.GetDuration());
        EXPECT_EQ(a.value, 0.0f);
        EXPECT_EQ(calls.ids.size(), static_cast<size_t>(kFour));
        EXPECT_TRUE(timeline.IsFinished());
    }

    namespace {

        constexpr float kStepSixtyFourth = 1.0f / 64.0f;
        constexpr float kFrameTime = 0.0166f;  // not a power of two: the sums round
        constexpr int kFrames = 200000;
        constexpr int kThousand = 1000;
        constexpr int kSix = 6;
        constexpr float kPulseValueTolerance = 1e-3f;

        /// A pulse of one tween (0 to 10 over a half second) and a callback at its end, repeated forever
        Timeline MakePulse(Property& property, Calls& calls) {
            Timeline timeline;
            timeline.Append<float>(0.0f, kFirstTo, Settings(kHalf), property.Setter()).AppendCallback(calls.Callback(1)).RepeatForever();
            return timeline;
        }

    }  // namespace

    TEST(TimelineTest, AnEndlessTimelineHasAnInfiniteDurationAndIsNeverFinishedByItself) {
        Property a;
        Calls calls;
        Timeline timeline = MakePulse(a, calls);
        EXPECT_TRUE(timeline.IsEndless());
        EXPECT_EQ(timeline.GetDuration(), std::numeric_limits<float>::infinity());

        timeline.Evaluate(kThousand * kHalf + kQuarter);
        EXPECT_FALSE(timeline.IsFinished());
        const int writes = a.writes;
        timeline.Evaluate(std::numeric_limits<float>::infinity());  // not a time an endless run can reach
        EXPECT_EQ(a.writes, writes);
        EXPECT_FALSE(timeline.IsFinished());

        Timeline empty;
        empty.RepeatForever();
        EXPECT_TRUE(empty.IsEndless());
        EXPECT_EQ(empty.GetDuration(), 0.0f);
    }

    TEST(TimelineTest, AThousandIterationsInOneEvaluateFireEveryCallbackOnceAndLeaveTheValueOfTheIterationItIsIn) {
        Property a;
        Calls calls;
        Timeline timeline = MakePulse(a, calls);

        timeline.Evaluate(static_cast<float>(kThousand) * kHalf + kQuarter);

        EXPECT_EQ(calls.ids.size(), static_cast<size_t>(kThousand));
        EXPECT_EQ(a.value, kFirstTo / 2);
        EXPECT_FALSE(timeline.IsFinished());
    }

    TEST(TimelineTest, AnEndlessTimelineWithNoLengthIsDoneByItsFirstCallAndDoesNotLoop) {
        Calls calls;
        Timeline timeline;
        timeline.AppendCallback(calls.Callback(1)).RepeatForever();
        EXPECT_EQ(timeline.GetDuration(), 0.0f);

        timeline.Evaluate(kOneSecond);
        EXPECT_EQ(calls.ids.size(), 1u);
        EXPECT_TRUE(timeline.IsFinished());
        timeline.Evaluate(kTwo);
        EXPECT_EQ(calls.ids.size(), 1u);

        Calls advanced;
        Timeline other;
        other.AppendCallback(advanced.Callback(1)).RepeatForever();
        other.Advance(kOneSecond);
        other.Advance(kOneSecond);
        EXPECT_EQ(advanced.ids.size(), 1u);
        EXPECT_TRUE(other.IsFinished());
    }

    TEST(TimelineTest, TheLastOfRepeatAndRepeatForeverWins) {
        Calls finite;
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kHalf), a.Setter()).AppendCallback(finite.Callback(1)).RepeatForever().Repeat(kRepeats);
        EXPECT_FALSE(timeline.IsEndless());
        EXPECT_EQ(timeline.GetDuration(), kOneSecond + kHalf);
        timeline.Evaluate(kOneSecond + kHalf);
        EXPECT_TRUE(timeline.IsFinished());
        EXPECT_EQ(finite.ids.size(), static_cast<size_t>(kRepeats));

        Calls endless;
        Timeline other;
        other.Append<float>(0.0f, kFirstTo, Settings(kHalf), a.Setter()).AppendCallback(endless.Callback(1)).Repeat(kRepeats).RepeatForever();
        other.Evaluate(kOneSecond + kHalf);
        EXPECT_FALSE(other.IsFinished());
        EXPECT_EQ(endless.ids.size(), static_cast<size_t>(kRepeats));
        other.Evaluate(kTwo + kHalf);
        EXPECT_FALSE(other.IsFinished());
        EXPECT_EQ(endless.ids.size(), static_cast<size_t>(kMoreRepeats));
    }

    TEST(TimelineTest, AnEndlessYoyoFlipsAtEveryBoundaryAndEndsEveryBackwardLegOnFrom) {
        Trace trace;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kHalf, 0.0f, EaseType::QuadIn), trace.Setter(1)).RepeatForever().Yoyo();

        for (int boundary = 0; boundary < kSix; ++boundary) {
            const float start = static_cast<float>(boundary) * kHalf;
            timeline.Evaluate(start + kEighth);
            // QuadIn at progress 1/4 forward, at 3/4 backward
            EXPECT_EQ(trace.writes.back().second, boundary % 2 == 0 ? kFirstTo * kQuadInAtQuarter : kFirstTo * kQuadInAtThreeQuarters) << boundary;

            const size_t before = trace.writes.size();
            timeline.Evaluate(start + kHalf);  // the end of this iteration and the start of the next, in one call
            ASSERT_EQ(trace.writes.size() - before, 2u) << boundary;
            const float edge = boundary % 2 == 0 ? kFirstTo : 0.0f;
            EXPECT_EQ(trace.writes[before].second, edge) << boundary;      // the last call of the leg
            EXPECT_EQ(trace.writes[before + 1].second, edge) << boundary;  // the first call of the next one
        }
        EXPECT_FALSE(timeline.IsFinished());
    }

    TEST(TimelineTest, AParentPlaysOneIterationOfAnEndlessSubTimeline) {
        for (const bool yoyo : {false, true}) {
            Property a;
            Property b;
            Timeline sub;
            sub.Append<float>(0.0f, kFirstTo, Settings(kHalf), a.Setter()).RepeatForever().Yoyo();
            Timeline timeline;
            timeline.Append(std::move(sub)).Append<float>(0.0f, kSecondTo, Settings(kQuarter), b.Setter());
            if (yoyo) {
                timeline.Repeat(2).Yoyo();
            }
            EXPECT_EQ(timeline.GetDuration(), yoyo ? kThreeQuarters * 2 : kThreeQuarters);

            timeline.Evaluate(kHalf + kEighth);  // the clip after the sub-timeline starts when ONE iteration of it ends
            EXPECT_EQ(a.value, kFirstTo);
            EXPECT_EQ(b.value, kFirstTo);  // 0 to 20, half way

            timeline.Evaluate(timeline.GetDuration());
            EXPECT_TRUE(timeline.IsFinished()) << yoyo;
            EXPECT_EQ(a.value, yoyo ? 0.0f : kFirstTo);
            EXPECT_EQ(b.value, yoyo ? 0.0f : kSecondTo);
        }
    }

    TEST(TimelineTest, AdvanceGivesTheWritesOfEvaluateOfTheRunningSum) {
        const auto build = [](Trace& trace, Calls& calls, int kind) {
            Timeline timeline;
            timeline.Append<float>(0.0f, kFirstTo, Settings(kHalf, kEighth, EaseType::QuadIn), trace.Setter(1)).InsertCallback(kQuarter, calls.Callback(1)).AppendCallback(calls.Callback(2));
            if (kind == 0) {
                timeline.Repeat(kRepeats);
            } else if (kind == 1) {
                timeline.Repeat(kMoreRepeats).Yoyo();
            } else {
                timeline.RepeatForever().Yoyo();
            }
            return timeline;
        };
        for (int kind = 0; kind < kRepeats; ++kind) {
            Trace advanced;
            Calls advancedCalls;
            Timeline advancing = build(advanced, advancedCalls, kind);
            Trace evaluated;
            Calls evaluatedCalls;
            Timeline evaluating = build(evaluated, evaluatedCalls, kind);

            float elapsed = 0.0f;
            constexpr int kSteps = 400;
            for (int step = 0; step < kSteps; ++step) {
                elapsed += kStepSixtyFourth;
                advancing.Advance(kStepSixtyFourth);
                evaluating.Evaluate(elapsed);
            }

            EXPECT_EQ(advancing.IsFinished(), evaluating.IsFinished()) << kind;
            EXPECT_EQ(kind != 2, advancing.IsFinished()) << kind;
            EXPECT_EQ(advancedCalls.ids.size(), evaluatedCalls.ids.size()) << kind;
            EXPECT_TRUE(advancedCalls.ids == evaluatedCalls.ids) << kind;
            ASSERT_EQ(advanced.writes.size(), evaluated.writes.size()) << kind;
            for (size_t index = 0; index < advanced.writes.size(); ++index) {
                EXPECT_EQ(advanced.writes[index].second, evaluated.writes[index].second) << kind << " write " << index;
            }
        }
    }

    TEST(TimelineTest, AnAdvanceOverSeveralIterationsCrossesEveryOneOfThem) {
        Property a;
        Calls calls;
        Timeline timeline = MakePulse(a, calls);
        timeline.Advance(kFirstTo + kQuarter);  // 20 iterations and a quarter of one
        EXPECT_EQ(calls.ids.size(), 20u);
        EXPECT_EQ(a.value, kFirstTo / 2);
        timeline.Advance(kQuarter);
        EXPECT_EQ(calls.ids.size(), 21u);
        EXPECT_EQ(a.value, 0.0f);  // the iteration that has just begun
    }

    TEST(TimelineTest, AdvanceKeepsItsPrecisionOverALongRun) {
        Property a;
        Calls calls;
        Timeline timeline = MakePulse(a, calls);
        for (int frame = 0; frame < kFrames; ++frame) {
            timeline.Advance(kFrameTime);
        }

        const double elapsed = static_cast<double>(kFrames) * static_cast<double>(kFrameTime);
        const double iterations = elapsed / static_cast<double>(kHalf);
        EXPECT_LE(std::abs(static_cast<double>(calls.ids.size()) - std::floor(iterations)), 1.0);
        const double phase = iterations - std::floor(iterations);  // the progress of the tween in its iteration
        EXPECT_NEAR(a.value, kFirstTo * phase, kPulseValueTolerance);
    }

    TEST(TimelineTest, ANegativeOrNanDtChangesNothing) {
        Property a;
        Calls calls;
        Timeline timeline = MakePulse(a, calls);
        timeline.Advance(kHalf + kQuarter);
        const float value = a.value;
        const size_t count = calls.ids.size();

        timeline.Advance(-kOneSecond);
        timeline.Advance(std::numeric_limits<float>::quiet_NaN());
        timeline.Advance(std::numeric_limits<float>::infinity());

        EXPECT_EQ(a.value, value);
        EXPECT_EQ(calls.ids.size(), count);
        timeline.Advance(kQuarter);
        EXPECT_EQ(calls.ids.size(), count + 1);
    }

    TEST(TimelineTest, ResetAfterAnEndlessRunStartsAgainFromTheFirstClip) {
        Property a;
        Calls calls;
        Timeline timeline = MakePulse(a, calls);
        timeline.Advance(kRepeats + kQuarter + kEighth);
        timeline.Reset();
        EXPECT_FALSE(timeline.IsFinished());

        calls.ids.clear();
        timeline.Advance(kEighth);
        EXPECT_EQ(a.value, kFirstTo / 4);
        EXPECT_TRUE(calls.ids.empty());
        timeline.Advance(kHalf);
        EXPECT_EQ(calls.ids.size(), 1u);
    }

    TEST(TimelineTest, AFiniteTimelineFinishesByAdvanceWithItsExactFinalValues) {
        Property a;
        Calls calls;
        Timeline timeline;
        timeline.Append<float>(0.0f, kFirstTo, Settings(kThirtyFiveHundredths), a.Setter()).AppendCallback(calls.Callback(1)).Repeat(kRepeats);
        constexpr int kStepLimit = 100;
        for (int step = 0; step < kStepLimit && !timeline.IsFinished(); ++step) {
            timeline.Advance(kTenth / 2);
        }
        EXPECT_TRUE(timeline.IsFinished());
        EXPECT_EQ(a.value, kFirstTo);
        EXPECT_EQ(calls.ids.size(), static_cast<size_t>(kRepeats));
    }

}  // namespace BECore::Tests
