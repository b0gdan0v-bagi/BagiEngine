#include <CoreVulkan/VulkanTimestampPolicy.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace VulkanTimestampPolicy;

    TEST(VulkanTimestampPolicyTest, TheMaskKeepsTheValidBits) {
        static_assert(ValidBitsMask(0) == 0);
        static_assert(ValidBitsMask(36) == 0xF'FFFF'FFFFull);
        static_assert(ValidBitsMask(63) == 0x7FFF'FFFF'FFFF'FFFFull);
        static_assert(ValidBitsMask(64) == UINT64_MAX);
        EXPECT_EQ(ValidBitsMask(8), 0xFFull);
    }

    TEST(VulkanTimestampPolicyTest, ATickIsThePeriodInNanoseconds) {
        EXPECT_DOUBLE_EQ(ElapsedMs(1'000, 2'001'000, 64, 1.0f), 2.0);
        constexpr float kPeriodNs = 41.5f;
        EXPECT_DOUBLE_EQ(ElapsedMs(0, 1'000'000, 64, kPeriodNs), 41.5);
        EXPECT_DOUBLE_EQ(ElapsedMs(7, 7, 64, 1.0f), 0.0);
    }

    TEST(VulkanTimestampPolicyTest, ACounterThatWrappedOnceStillGivesTheSpan) {
        constexpr uint32_t kBits = 36;
        const uint64_t top = ValidBitsMask(kBits);
        EXPECT_DOUBLE_EQ(ElapsedMs(top - 999'999, 1'000'000, kBits, 1.0f), 2.0);
        EXPECT_DOUBLE_EQ(ElapsedMs(UINT64_MAX - 999'999, 1'000'000, 64, 1.0f), 2.0);
    }

    TEST(VulkanTimestampPolicyTest, TheBitsAboveTheValidOnesAreIgnored) {
        constexpr uint64_t kNoise = 0xABCDull << 40;
        EXPECT_DOUBLE_EQ(ElapsedMs(kNoise | 1'000, 3'001'000, 36, 1.0f), 3.0);
    }

    TEST(VulkanTimestampPolicyTest, AfterIsLessThanHalfTheRangeAhead) {
        static_assert(IsAfter(11, 10, 36));
        static_assert(!IsAfter(10, 10, 36));
        static_assert(!IsAfter(10, 11, 36));
        static_assert(IsAfter(5, ValidBitsMask(36) - 5, 36), "a counter that wrapped goes on");
        static_assert(!IsAfter(ValidBitsMask(36) - 5, 5, 36));
        EXPECT_TRUE(IsAfter(5, UINT64_MAX - 5, 64));
    }

    TEST(VulkanTimestampPolicyTest, TheFirstSpanIsTakenWhenItDoesNotEndBeforeItBegins) {
        EXPECT_TRUE(IsNewSpan(nullptr, {100, 200}, 64));
        EXPECT_TRUE(IsNewSpan(nullptr, {100, 100}, 64));
        EXPECT_FALSE(IsNewSpan(nullptr, {200, 100}, 64));
        EXPECT_TRUE(IsNewSpan(nullptr, {ValidBitsMask(36) - 10, 20}, 36)) << "the counter wrapped inside the frame";
    }

    TEST(VulkanTimestampPolicyTest, ASpanOfALaterFrameIsNew) {
        const TimestampSpan previous{100, 200};
        EXPECT_TRUE(IsNewSpan(&previous, {300, 400}, 64));
        EXPECT_TRUE(IsNewSpan(&previous, {150, 250}, 64)) << "frames may overlap";
    }

    TEST(VulkanTimestampPolicyTest, TheStampsOfAnEarlierFrameAreNotNew) {
        const TimestampSpan previous{1'000, 1'100};
        EXPECT_FALSE(IsNewSpan(&previous, previous, 64)) << "the same result again";
        EXPECT_FALSE(IsNewSpan(&previous, {500, 600}, 64)) << "an older frame";
        EXPECT_FALSE(IsNewSpan(&previous, {2'000, 600}, 64)) << "a new begin with an old end";
        EXPECT_FALSE(IsNewSpan(&previous, {500, 2'100}, 64)) << "an old begin with a new end";
    }

    TEST(VulkanTimestampPolicyTest, WithoutValidBitsOrATickLengthThereIsNoTime) {
        static_assert(CanTime(36, 1.0f));
        static_assert(!CanTime(0, 1.0f));
        static_assert(!CanTime(36, 0.0f));
        static_assert(!CanTime(36, -1.0f));
        EXPECT_DOUBLE_EQ(ElapsedMs(0, 1'000'000, 0, 1.0f), 0.0);
        EXPECT_DOUBLE_EQ(ElapsedMs(0, 1'000'000, 36, 0.0f), 0.0);
    }

}  // namespace BECore::Tests
