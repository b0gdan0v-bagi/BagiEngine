#include <Rooms/RateLimiter.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using std::chrono::milliseconds;
        using std::chrono::seconds;

        const RoomTime kStart{seconds(1000)};

        int CountTaken(RateLimiter& limiter, RoomTime now, int attempts) {
            int taken = 0;
            for (int i = 0; i < attempts; ++i) {
                if (limiter.TryTake(now)) {
                    ++taken;
                }
            }
            return taken;
        }

    }  // namespace

    // PeekTokens refills a copy, so the limiter must stay copyable
    static_assert(std::is_copy_constructible_v<RateLimiter>);

    TEST(RateLimiterTest, BurstIsAllowedAtOnceThenRefused) {
        RateLimiter limiter(3, 10.0);
        EXPECT_TRUE(limiter.TryTake(kStart));
        EXPECT_TRUE(limiter.TryTake(kStart));
        EXPECT_TRUE(limiter.TryTake(kStart));
        EXPECT_FALSE(limiter.TryTake(kStart));
        EXPECT_FALSE(limiter.TryTake(kStart));
    }

    TEST(RateLimiterTest, DefaultsAreTheRoomLimits) {
        RateLimiter limiter;
        EXPECT_EQ(CountTaken(limiter, kStart, 100), RoomLimits::kRateBurst);
    }

    TEST(RateLimiterTest, RefillsOverInjectedTime) {
        RateLimiter limiter(3, 10.0);
        EXPECT_EQ(CountTaken(limiter, kStart, 10), 3);
        // 100 ms at 10 per second is one token
        EXPECT_TRUE(limiter.TryTake(kStart + milliseconds(100)));
        EXPECT_FALSE(limiter.TryTake(kStart + milliseconds(100)));
        // 300 ms more is three tokens
        EXPECT_EQ(CountTaken(limiter, kStart + milliseconds(400), 10), 3);
    }

    TEST(RateLimiterTest, PartTokensAddUp) {
        RateLimiter limiter(1, 4.0);
        EXPECT_TRUE(limiter.TryTake(kStart));
        // 125 ms at 4 per second is half a token (exact in binary floating point)
        EXPECT_FALSE(limiter.TryTake(kStart + milliseconds(125)));
        EXPECT_TRUE(limiter.TryTake(kStart + milliseconds(250)));
        EXPECT_FALSE(limiter.TryTake(kStart + milliseconds(250)));
    }

    TEST(RateLimiterTest, BucketNeverHoldsMoreThanTheBurst) {
        RateLimiter limiter(3, 10.0);
        EXPECT_EQ(CountTaken(limiter, kStart, 3), 3);
        EXPECT_EQ(CountTaken(limiter, kStart + std::chrono::hours(10), 10), 3);
    }

    TEST(RateLimiterTest, SteadyRateIsHeldOverALongRun) {
        constexpr int kBurst = 5;
        // 50 attempts a second for 20 seconds: the burst, then one token per 100 ms of the 19.98 s after the first attempt
        constexpr int kRefilled = 199;
        RateLimiter limiter(kBurst, 10.0);
        int taken = 0;
        for (int step = 0; step < 1000; ++step) {
            if (limiter.TryTake(kStart + milliseconds(step * 20))) {
                ++taken;
            }
        }
        EXPECT_EQ(taken, kBurst + kRefilled);
    }

    TEST(RateLimiterTest, TimeGoingBackAddsNothing) {
        RateLimiter limiter(2, 10.0);
        EXPECT_EQ(CountTaken(limiter, kStart + seconds(10), 5), 2);
        // An earlier time must not add tokens or break the next refill
        EXPECT_FALSE(limiter.TryTake(kStart));
        EXPECT_FALSE(limiter.TryTake(kStart - seconds(500)));
        EXPECT_FALSE(limiter.TryTake(kStart + seconds(10)));
        // The refill goes on from the latest time seen
        EXPECT_TRUE(limiter.TryTake(kStart + seconds(10) + milliseconds(100)));
        EXPECT_FALSE(limiter.TryTake(kStart + seconds(10) + milliseconds(100)));
    }

    TEST(RateLimiterTest, StartsFullWhenFirstUsedLate) {
        RateLimiter limiter(4, 1.0);
        EXPECT_EQ(CountTaken(limiter, kStart + std::chrono::hours(5), 10), 4);
    }

    TEST(RateLimiterTest, OddSettingsAreClamped) {
        RateLimiter noBurst(0, 10.0);
        EXPECT_EQ(CountTaken(noBurst, kStart, 5), 1);
        RateLimiter negativeBurst(-7, 10.0);
        EXPECT_EQ(CountTaken(negativeBurst, kStart, 5), 1);
        RateLimiter noRefill(2, 0.0);
        EXPECT_EQ(CountTaken(noRefill, kStart, 5), 2);
        EXPECT_EQ(CountTaken(noRefill, kStart + std::chrono::hours(1), 5), 0);
        RateLimiter negativeRate(2, -5.0);
        EXPECT_EQ(CountTaken(negativeRate, kStart, 5), 2);
        EXPECT_EQ(CountTaken(negativeRate, kStart + seconds(100), 5), 0);
        RateLimiter notANumber(2, std::numeric_limits<double>::quiet_NaN());
        EXPECT_EQ(CountTaken(notANumber, kStart, 5), 2);
        EXPECT_EQ(CountTaken(notANumber, kStart + seconds(100), 5), 0);
        RateLimiter infinite(2, std::numeric_limits<double>::infinity());
        EXPECT_EQ(CountTaken(infinite, kStart, 5), 2);
        EXPECT_EQ(CountTaken(infinite, kStart + milliseconds(1), 5), 2);
    }

    TEST(RateLimiterTest, PeekDoesNotTake) {
        RateLimiter limiter(3, 10.0);
        EXPECT_DOUBLE_EQ(limiter.PeekTokens(kStart), 3.0);
        EXPECT_DOUBLE_EQ(limiter.PeekTokens(kStart), 3.0);
        EXPECT_EQ(CountTaken(limiter, kStart, 10), 3);
        EXPECT_NEAR(limiter.PeekTokens(kStart + milliseconds(500)), 3.0, 1e-9);
        EXPECT_NEAR(limiter.PeekTokens(kStart + milliseconds(500)), 3.0, 1e-9);
    }

    TEST(RateLimiterTest, CanTakeLooksWithoutTaking) {
        RateLimiter limiter(2, 1.0);
        EXPECT_TRUE(limiter.CanTake(kStart));
        EXPECT_TRUE(limiter.CanTake(kStart));
        EXPECT_TRUE(limiter.TryTake(kStart));
        EXPECT_TRUE(limiter.TryTake(kStart));
        EXPECT_FALSE(limiter.CanTake(kStart));
        EXPECT_FALSE(limiter.CanTake(kStart + milliseconds(900)));
        EXPECT_TRUE(limiter.CanTake(kStart + milliseconds(1000)));
        // Looking did not move the clock of the bucket
        EXPECT_FALSE(limiter.TryTake(kStart));
        EXPECT_TRUE(limiter.TryTake(kStart + milliseconds(1000)));
    }

    TEST(RateLimiterTest, SpendTakesOneWhenThereIsOneAndNothingWhenEmpty) {
        RateLimiter limiter(2, 0.0);
        limiter.Spend(kStart);
        EXPECT_NEAR(limiter.PeekTokens(kStart), 1.0, 1e-9);
        limiter.Spend(kStart);
        limiter.Spend(kStart);
        EXPECT_NEAR(limiter.PeekTokens(kStart), 0.0, 1e-9);
        EXPECT_FALSE(limiter.TryTake(kStart));
    }

    TEST(RateLimiterTest, LimitersAreIndependent) {
        RateLimiter first(1, 1.0);
        RateLimiter second(1, 1.0);
        EXPECT_TRUE(first.TryTake(kStart));
        EXPECT_FALSE(first.TryTake(kStart));
        EXPECT_TRUE(second.TryTake(kStart));
    }

}  // namespace BECore::Tests
