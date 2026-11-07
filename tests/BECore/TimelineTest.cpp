#include <BECore/Animation/Timeline.h>
#include <cmath>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    static_assert(!std::is_copy_constructible_v<Timeline>);
    static_assert(!std::is_copy_assignable_v<Timeline>);
    static_assert(std::is_nothrow_move_constructible_v<Timeline>);
    static_assert(std::is_nothrow_move_assignable_v<Timeline>);

    TEST(TimelineTest, CompileTimeChecks) {}

    namespace {

        // Times and durations are powers of two, so the sums are exact in floating point
        constexpr float kNever = -1.0f;  // the value of a property nobody has written yet
        constexpr float kEighth = 0.125f;
        constexpr float kQuarter = 0.25f;
        constexpr float kHalf = 0.5f;
        constexpr float kThreeQuarters = 0.75f;
        constexpr float kOne = 1.0f;
        constexpr float kTen = 10.0f;
        constexpr float kTwenty = 20.0f;
        constexpr float kThirty = 30.0f;
        constexpr float kQuadInAtHalf = 0.25f;  // Ease(QuadIn, 0.5)
        constexpr int kThree = 3;

        TweenSettings Settings(float duration, float delay = 0.0f, EaseType ease = EaseType::Linear) {
            return {duration, delay, ease};
        }

        /// A float property that records every value written to it
        struct Property {
            float value = kNever;
            int writes = 0;

            eastl::function<void(const float&)> Setter() {
                return [this](const float& written) {
                    value = written;
                    ++writes;
                };
            }
        };

        /// What the callbacks saw, in order
        struct Calls {
            eastl::vector<int> ids;

            eastl::function<void()> Callback(int id) {
                return [this, id] { ids.push_back(id); };
            }
        };

        bool operator==(const eastl::vector<int>& left, std::initializer_list<int> right) {
            return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
        }

    }  // namespace

    TEST(TimelineTest, AppendIsASequence) {
        Property a;
        Property b;
        Property c;
        Timeline timeline;
        timeline.Append<float>(0.0f, kTen, Settings(kQuarter), a.Setter()).Append<float>(0.0f, kTwenty, Settings(kQuarter), b.Setter()).Append<float>(0.0f, kThirty, Settings(kQuarter), c.Setter());
        EXPECT_EQ(timeline.GetDuration(), kThreeQuarters);

        timeline.Evaluate(kEighth);
        EXPECT_EQ(a.value, kTen / 2);
        EXPECT_EQ(b.writes, 0);
        EXPECT_EQ(c.writes, 0);

        // The end of the first clip is the start of the second: the first is final, the second is entered at its from
        timeline.Evaluate(kQuarter);
        EXPECT_EQ(a.value, kTen);
        EXPECT_EQ(b.value, 0.0f);
        EXPECT_EQ(c.writes, 0);

        timeline.Evaluate(kHalf);
        EXPECT_EQ(b.value, kTwenty);
        EXPECT_EQ(c.value, 0.0f);

        timeline.Evaluate(kThreeQuarters);
        EXPECT_EQ(c.value, kThirty);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, JoinStartsWithThePreviousClipAndTheDurationIsTheLongerOne) {
        Property a;
        Property b;
        Property c;
        Timeline timeline;
        timeline.Append<float>(0.0f, kTen, Settings(kHalf), a.Setter()).Join<float>(0.0f, kTwenty, Settings(kQuarter), b.Setter()).Append<float>(0.0f, kThirty, Settings(kQuarter), c.Setter());
        EXPECT_EQ(timeline.GetDuration(), kThreeQuarters);

        timeline.Evaluate(kQuarter);
        EXPECT_EQ(a.value, kTen / 2);
        EXPECT_EQ(b.value, kTwenty);
        EXPECT_EQ(c.writes, 0);

        // `Append` after a shorter joined clip waits for the longer one: the end is the latest end
        timeline.Evaluate(kHalf);
        EXPECT_EQ(a.value, kTen);
        EXPECT_EQ(c.value, 0.0f);
    }

    TEST(TimelineTest, JoinOnAnEmptyTimelineStartsAtZero) {
        Property a;
        Timeline timeline;
        timeline.Join<float>(0.0f, kTen, Settings(kQuarter), a.Setter());
        timeline.Evaluate(kEighth);
        EXPECT_EQ(a.value, kTen / 2);
    }

    TEST(TimelineTest, InsertPlacesAClipAtAnAbsoluteTimeAndExtendsTheDuration) {
        Property a;
        Property b;
        Timeline timeline;
        timeline.Append<float>(0.0f, kTen, Settings(kQuarter), a.Setter()).Insert<float>(kHalf, 0.0f, kTwenty, Settings(kQuarter), b.Setter());
        EXPECT_EQ(timeline.GetDuration(), kThreeQuarters);

        timeline.Evaluate(kQuarter);
        EXPECT_EQ(b.writes, 0);
        timeline.Evaluate(kHalf + kEighth);
        EXPECT_EQ(b.value, kTwenty / 2);
    }

    TEST(TimelineTest, InsertBetweenTwoStartsKeepsTheStartOrder) {
        Property a;
        Property b;
        Timeline timeline;
        timeline.Append<float>(0.0f, kTen, Settings(kQuarter), a.Setter()).Append<float>(0.0f, kTwenty, Settings(kQuarter), b.Setter());
        Calls calls;
        // At 0.125, between the two clips' starts; the callback is visited after the first clip and before the second
        timeline.InsertCallback(kEighth, calls.Callback(1));
        timeline.Evaluate(kThreeQuarters);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1}));
        EXPECT_EQ(a.value, kTen);
        EXPECT_EQ(b.value, kTwenty);
    }

    TEST(TimelineTest, AppendDelayIsASpacerThatCallsNothing) {
        Property a;
        Property b;
        Timeline timeline;
        timeline.Append<float>(0.0f, kTen, Settings(kQuarter), a.Setter()).AppendDelay(kQuarter).Append<float>(0.0f, kTwenty, Settings(kQuarter), b.Setter());
        EXPECT_EQ(timeline.GetDuration(), kThreeQuarters);

        timeline.Evaluate(kQuarter);
        const int writes = a.writes;
        timeline.Evaluate(kQuarter + kEighth);  // inside the spacer
        EXPECT_EQ(a.writes, writes);
        EXPECT_EQ(b.writes, 0);

        timeline.Evaluate(kHalf + kEighth);
        EXPECT_EQ(b.value, kTwenty / 2);
    }

    TEST(TimelineTest, CallbacksFireOnceAtTheirTimeInOrderWithTheClips) {
        Calls calls;
        Property a;
        Timeline timeline;
        timeline.AppendCallback(calls.Callback(1)).Append<float>(0.0f, kTen, Settings(kQuarter), [&](const float& value) {
            a.value = value;
            calls.ids.push_back(2);
        });
        timeline.AppendCallback(calls.Callback(3));
        EXPECT_EQ(timeline.GetDuration(), kQuarter);

        timeline.Evaluate(kEighth);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2}));
        timeline.Evaluate(kQuarter);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2, 2, 3}));
        timeline.Evaluate(kOne);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2, 2, 3}));
    }

    TEST(TimelineTest, AJumpOverSeveralCallbacksFiresThemAllInOrder) {
        Calls calls;
        Timeline timeline;
        timeline.InsertCallback(kHalf, calls.Callback(3)).InsertCallback(kQuarter, calls.Callback(2)).InsertCallback(0.0f, calls.Callback(1));
        timeline.Evaluate(kOne);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2, 3}));
    }

    TEST(TimelineTest, SetterCallsFollowTheEaseOfTheLocalProgressAndTheLastIsExactlyTo) {
        Property a;
        const float to = 0.1f;
        Timeline timeline;
        timeline.Append<float>(0.0f, to, Settings(kOne, 0.0f, EaseType::QuadIn), a.Setter());

        timeline.Evaluate(kHalf);
        EXPECT_EQ(a.value, to * kQuadInAtHalf);
        timeline.Evaluate(kOne);
        EXPECT_EQ(a.value, to);
        EXPECT_EQ(a.writes, 2);
    }

    TEST(TimelineTest, AJumpOverAWholeClipStillGivesOneFinalSetter) {
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kTen, Settings(kQuarter), a.Setter()).AppendDelay(kQuarter);
        timeline.Evaluate(kHalf);
        EXPECT_EQ(a.writes, 1);
        EXPECT_EQ(a.value, kTen);
    }

    TEST(TimelineTest, AJumpOverTheWholeTimelineLeavesTheValueOfTheLastWriter) {
        Property a;
        Property b;
        Timeline timeline;
        timeline.Append<float>(0.0f, kTen, Settings(kQuarter), a.Setter()).Append<float>(0.0f, kTwenty, Settings(kQuarter), a.Setter()).Join<float>(0.0f, kThirty, Settings(kQuarter), b.Setter());
        timeline.Evaluate(kThirty);
        EXPECT_EQ(a.value, kTwenty);
        EXPECT_EQ(a.writes, 2);
        EXPECT_EQ(b.value, kThirty);
    }

    TEST(TimelineTest, EvaluatingTheDurationFinishesEverythingOnceAndLaterCallsChangeNothing) {
        Property a;
        Calls calls;
        Timeline timeline;
        timeline.Append<float>(0.0f, kTen, Settings(kQuarter), a.Setter()).AppendCallback(calls.Callback(1));
        EXPECT_FALSE(timeline.IsFinished());

        timeline.Evaluate(timeline.GetDuration());
        EXPECT_TRUE(timeline.IsFinished());
        EXPECT_EQ(a.writes, 1);
        EXPECT_EQ(calls.ids.size(), 1u);

        timeline.Evaluate(timeline.GetDuration());
        timeline.Evaluate(kOne);
        EXPECT_EQ(a.writes, 1);
        EXPECT_EQ(calls.ids.size(), 1u);
    }

    TEST(TimelineTest, Vec2AndColorClips) {
        Vec2 position;
        Color color;
        Timeline timeline;
        timeline.Append<Vec2>({0.0f, 0.0f}, {kTen, kTwenty}, Settings(kQuarter), [&](const Vec2& value) { position = value; });
        timeline.Join<Color>({0, 0, 0, 0}, {200, 100, 50, 255}, Settings(kQuarter), [&](const Color& value) { color = value; });

        timeline.Evaluate(kEighth);
        EXPECT_EQ(position, Vec2(kTen / 2, kTwenty / 2));
        EXPECT_EQ(color.r, 100);
        EXPECT_EQ(color.g, 50);

        timeline.Evaluate(kQuarter);
        EXPECT_EQ(position, Vec2(kTen, kTwenty));
        EXPECT_EQ(color.r, 200);
        EXPECT_EQ(color.a, 255);
    }

    TEST(TimelineTest, AClipDelayLengthensItsSlot) {
        Property a;
        Property b;
        Timeline timeline;
        timeline.Append<float>(0.0f, kTen, Settings(kQuarter, kQuarter), a.Setter()).Append<float>(0.0f, kTwenty, Settings(kQuarter), b.Setter());
        EXPECT_EQ(timeline.GetDuration(), kThreeQuarters);

        timeline.Evaluate(kEighth);  // inside the delay: no setter call
        EXPECT_EQ(a.writes, 0);
        timeline.Evaluate(kQuarter + kEighth);
        EXPECT_EQ(a.value, kTen / 2);
        EXPECT_EQ(b.writes, 0);
        timeline.Evaluate(kHalf);
        EXPECT_EQ(a.value, kTen);
        EXPECT_EQ(b.value, 0.0f);
    }

    TEST(TimelineTest, ASubTimelineIsAClipWithItsOwnLocalTime) {
        Property inner;
        Property outer;
        Calls calls;
        Timeline sub;
        sub.AppendCallback(calls.Callback(1)).Append<float>(0.0f, kTen, Settings(kQuarter), inner.Setter()).AppendCallback(calls.Callback(2));

        Timeline timeline;
        timeline.Append<float>(0.0f, kTwenty, Settings(kQuarter), outer.Setter()).Append(std::move(sub));
        EXPECT_EQ(timeline.GetDuration(), kHalf);

        timeline.Evaluate(kQuarter - kEighth);
        EXPECT_EQ(calls.ids.size(), 0u);
        timeline.Evaluate(kQuarter + kEighth);  // the sub-timeline's time is 0.125
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1}));
        EXPECT_EQ(inner.value, kTen / 2);
        timeline.Evaluate(kHalf);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2}));
        EXPECT_EQ(inner.value, kTen);
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
        timeline.Append<float>(0.0f, kTen, Settings(kQuarter), a.Setter()).AppendCallback(calls.Callback(1)).Repeat(kThree);
        EXPECT_EQ(timeline.GetDuration(), kThreeQuarters);

        timeline.Evaluate(kQuarter);
        EXPECT_EQ(calls.ids.size(), 1u);
        // The second iteration starts from `from` again, with the left-over time
        timeline.Evaluate(kQuarter + kEighth);
        EXPECT_EQ(a.value, kTen / 2);
        timeline.Evaluate(kHalf);
        EXPECT_EQ(calls.ids.size(), 2u);
        EXPECT_FALSE(timeline.IsFinished());
        timeline.Evaluate(kThreeQuarters);
        EXPECT_EQ(calls.ids.size(), 3u);
        EXPECT_EQ(a.value, kTen);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, ABigJumpCrossesIterationsAndLosesNoCallback) {
        Property a;
        Calls calls;
        Timeline timeline;
        timeline.Append<float>(0.0f, kTen, Settings(kQuarter), a.Setter()).AppendCallback(calls.Callback(1)).Repeat(kThree);
        timeline.Evaluate(kOne);
        EXPECT_EQ(calls.ids.size(), 3u);
        EXPECT_EQ(a.writes, kThree);
        EXPECT_EQ(a.value, kTen);
        EXPECT_TRUE(timeline.IsFinished());

        // A jump that ends inside the second iteration
        Calls second;
        Property b;
        Timeline other;
        other.Append<float>(0.0f, kTen, Settings(kQuarter), b.Setter()).AppendCallback(second.Callback(1)).Repeat(kThree);
        other.Evaluate(kQuarter + kEighth);
        EXPECT_EQ(second.ids.size(), 1u);
        EXPECT_EQ(b.value, kTen / 2);
    }

    TEST(TimelineTest, ARepeatBelowOneIsOne) {
        for (const int count : {0, -1, -100}) {
            Calls calls;
            Timeline timeline;
            timeline.AppendDelay(kQuarter).AppendCallback(calls.Callback(1)).Repeat(count);
            EXPECT_EQ(timeline.GetDuration(), kQuarter) << count;
            timeline.Evaluate(kOne);
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
        repeated.AppendCallback(looped.Callback(1)).Repeat(5);
        repeated.Evaluate(kOne);
        EXPECT_TRUE(repeated.IsFinished());
        EXPECT_EQ(looped.ids.size(), 1u);

        Timeline empty;
        empty.Evaluate(kEighth);
        EXPECT_TRUE(empty.IsFinished());
    }

    TEST(TimelineTest, AZeroLengthClipGetsItsFinalSetterAtItsStart) {
        Property a;
        Timeline timeline;
        timeline.AppendDelay(kQuarter).Append<float>(0.0f, kTen, Settings(0.0f), a.Setter());
        timeline.Evaluate(kEighth);
        EXPECT_EQ(a.writes, 0);
        timeline.Evaluate(kQuarter);
        EXPECT_EQ(a.value, kTen);
        EXPECT_EQ(a.writes, 1);
    }

    TEST(TimelineTest, ResetMakesTheTimelineRunAgainFromTheStart) {
        Property a;
        Calls calls;
        Timeline sub;
        sub.AppendCallback(calls.Callback(2));
        Timeline timeline;
        timeline.Append<float>(0.0f, kTen, Settings(kQuarter), a.Setter()).AppendCallback(calls.Callback(1)).Append(std::move(sub));
        timeline.Evaluate(kOne);
        EXPECT_TRUE(timeline.IsFinished());
        EXPECT_EQ(calls.ids.size(), 2u);

        timeline.Reset();
        EXPECT_FALSE(timeline.IsFinished());
        timeline.Evaluate(kEighth);
        EXPECT_EQ(a.value, kTen / 2);
        timeline.Evaluate(kOne);
        EXPECT_EQ(calls.ids.size(), 4u);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({1, 2, 1, 2}));
    }

    TEST(TimelineTest, EqualStartTimesKeepTheInsertionOrder) {
        Calls calls;
        Timeline timeline;
        timeline.InsertCallback(kQuarter, calls.Callback(1)).InsertCallback(kQuarter, calls.Callback(2)).InsertCallback(0.0f, calls.Callback(0)).InsertCallback(kQuarter, calls.Callback(3));
        timeline.Evaluate(kOne);
        EXPECT_TRUE(calls.ids == std::initializer_list<int>({0, 1, 2, 3}));
    }

    TEST(TimelineTest, BadTimesCountAsZero) {
        constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
        constexpr float kInfinity = std::numeric_limits<float>::infinity();
        Property a;
        Calls calls;
        Timeline timeline;
        timeline.Append<float>(0.0f, kTen, Settings(kNan, kInfinity), a.Setter())
            .Insert<float>(-kOne, 0.0f, kTwenty, Settings(-kOne), a.Setter())
            .InsertCallback(kNan, calls.Callback(1))
            .AppendDelay(kNan);
        EXPECT_EQ(timeline.GetDuration(), 0.0f);
        timeline.Evaluate(0.0f);
        EXPECT_TRUE(timeline.IsFinished());
        EXPECT_EQ(a.value, kTwenty);
        EXPECT_EQ(calls.ids.size(), 1u);
    }

    TEST(TimelineTest, ANanTimeIsIgnoredAndANegativeOneIsZero) {
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kTen, Settings(kQuarter), a.Setter());
        timeline.Evaluate(std::numeric_limits<float>::quiet_NaN());
        EXPECT_EQ(a.writes, 0);
        timeline.Evaluate(-kOne);
        EXPECT_EQ(a.value, 0.0f);
        timeline.Evaluate(std::numeric_limits<float>::infinity());
        EXPECT_EQ(a.value, kTen);
        EXPECT_TRUE(timeline.IsFinished());
    }

    TEST(TimelineTest, AMovedTimelineKeepsItsClips) {
        Property a;
        Timeline timeline;
        timeline.Append<float>(0.0f, kTen, Settings(kQuarter), a.Setter());
        Timeline moved = std::move(timeline);
        EXPECT_EQ(moved.GetDuration(), kQuarter);
        moved.Evaluate(kQuarter);
        EXPECT_EQ(a.value, kTen);
    }

}  // namespace BECore::Tests
