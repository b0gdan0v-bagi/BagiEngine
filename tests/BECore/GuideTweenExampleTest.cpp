#include <BECore/Animation/Easing.h>
#include <BECore/Animation/ScopedTween.h>
#include <BECore/Animation/Timeline.h>
#include <BECore/Animation/TweenManager.h>
#include <BECore/Math/Vec2.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The page "a tween and a timeline" of the guide for people (`docs/guide/tween-and-timeline.md`) excerpts the class below. It is here and not
     * in the page so that it is compiled and run: a change of the shape of a timeline call fails this test before it misleads a reader. The
     * cases under it pin what the page says about the times.
     */
    namespace GuideTweenExample {

        /// Fades a value in while it moves a point, waits a moment, then counts one ping and tells its owner it is done
        class Appear {
        public:
            Appear(TweenManager& tweens, MoveOnlyFunction<void()> onDone) : _tweens(tweens), _onDone(std::move(onDone)) {}

            void Play(Vec2 startPos, Vec2 endPos) {
                Timeline timeline;
                timeline
                    .Append<float>(0.0f, 1.0f, {.duration = 0.25f, .ease = EaseType::QuadOut}, [this](const float& alpha) { _alpha = alpha; })  // 0.0 .. 0.25
                    .Join<Vec2>(startPos, endPos, {.duration = 0.25f}, [this](const Vec2& position) { _position = position; })                  // beside the one above
                    .AppendDelay(0.125f)                                                                                                        // 0.25 .. 0.375
                    .AppendCallback([this] { ++_pings; });                                                                                      // at 0.375
                _play.Reset(_tweens.Play(std::move(timeline), 0.0f, [this] { _onDone(); }));
            }

            void Stop() {
                _play.Reset();
            }

            [[nodiscard]] float GetAlpha() const {
                return _alpha;
            }

            [[nodiscard]] Vec2 GetPosition() const {
                return _position;
            }

            [[nodiscard]] int GetPings() const {
                return _pings;
            }

        private:
            TweenManager& _tweens;
            MoveOnlyFunction<void()> _onDone;
            float _alpha = 0.0f;
            Vec2 _position;
            int _pings = 0;
            ScopedTween _play;  // the setters point at this object: the destructor cancels them
        };

    }  // namespace GuideTweenExample

    namespace {

        using GuideTweenExample::Appear;

        // Times are powers of two, so the sums are exact in floating point
        constexpr float kEighth = 0.125f;
        constexpr float kQuarter = 0.25f;
        constexpr float kOneSecond = 1.0f;
        constexpr Vec2 kStart{0.0f, 0.0f};
        constexpr Vec2 kEnd{100.0f, 50.0f};
        constexpr Vec2 kHalfWay{50.0f, 25.0f};

        /// An Appear on its own manager and the counters of what its completion callback saw
        struct Fixture {
            TweenManager tweens;
            int done = 0;
            int pingsWhenDone = -1;
            Appear appear{tweens, [this] {
                              ++done;
                              pingsWhenDone = appear.GetPings();
                          }};

            Fixture() {
                appear.Play(kStart, kEnd);
            }
        };

    }  // namespace

    TEST(GuideTweenExampleTest, TheJoinedTweenRunsBesideTheFirstOne) {
        Fixture f;

        f.tweens.Advance(kEighth);

        EXPECT_FLOAT_EQ(f.appear.GetAlpha(), Ease(EaseType::QuadOut, 0.5f));
        EXPECT_EQ(f.appear.GetPosition(), kHalfWay);
    }

    TEST(GuideTweenExampleTest, WhenTheClipIsDoneTheValuesAreExactlyTheTargets) {
        Fixture f;

        f.tweens.Advance(kEighth);
        f.tweens.Advance(kEighth);

        EXPECT_EQ(f.appear.GetAlpha(), 1.0f);
        EXPECT_EQ(f.appear.GetPosition(), kEnd);
        EXPECT_EQ(f.appear.GetPings(), 0) << "the delay is still running";
        EXPECT_EQ(f.done, 0);
    }

    TEST(GuideTweenExampleTest, TheCallbackFiresOnceAtItsTimeAndTheCompletionRunsAfterIt) {
        Fixture f;

        f.tweens.Advance(kQuarter);
        EXPECT_EQ(f.appear.GetPings(), 0);

        f.tweens.Advance(kEighth);
        EXPECT_EQ(f.appear.GetPings(), 1);
        EXPECT_EQ(f.done, 1);
        EXPECT_EQ(f.pingsWhenDone, 1) << "the callback clip came before the completion";

        f.tweens.Advance(kOneSecond);
        EXPECT_EQ(f.appear.GetPings(), 1);
        EXPECT_EQ(f.done, 1);
        EXPECT_EQ(f.tweens.GetActiveCount(), 0u);
    }

    TEST(GuideTweenExampleTest, OneBigFrameLeavesEveryValueRight) {
        Fixture f;

        f.tweens.Advance(kOneSecond);

        EXPECT_EQ(f.appear.GetAlpha(), 1.0f);
        EXPECT_EQ(f.appear.GetPosition(), kEnd);
        EXPECT_EQ(f.appear.GetPings(), 1);
        EXPECT_EQ(f.done, 1);
    }

    TEST(GuideTweenExampleTest, StoppingSetsNothingAndRunsNoCallback) {
        Fixture f;
        f.tweens.Advance(kEighth);
        const float alpha = f.appear.GetAlpha();
        ASSERT_LT(alpha, 1.0f);

        f.appear.Stop();
        f.tweens.Advance(kOneSecond);

        EXPECT_EQ(f.appear.GetAlpha(), alpha) << "a cancel leaves the value where it was";
        EXPECT_EQ(f.appear.GetPosition(), kHalfWay);
        EXPECT_EQ(f.appear.GetPings(), 0);
        EXPECT_EQ(f.done, 0);
        EXPECT_EQ(f.tweens.GetActiveCount(), 0u);
    }

    TEST(GuideTweenExampleTest, ADestroyedOwnerLeavesNoTweenToWriteToIt) {
        TweenManager tweens;
        int done = 0;
        auto appear = MakeUnique<Appear>(tweens, [&done] { ++done; });
        appear->Play(kStart, kEnd);
        tweens.Advance(kEighth);
        ASSERT_EQ(tweens.GetActiveCount(), 1u);

        appear.reset();

        EXPECT_EQ(tweens.GetActiveCount(), 0u);
        tweens.Advance(kOneSecond);
        EXPECT_EQ(done, 0);
    }

}  // namespace BECore::Tests
