#include <CoreVulkan/VulkanFramePolicy.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace VulkanFramePolicy;

    TEST(VulkanFramePolicyTest, OnlySuccessGoesOn) {
        static_assert(StepAfter(VK_SUCCESS) == FrameStep::Proceed);
        EXPECT_EQ(StepAfter(VK_SUCCESS), FrameStep::Proceed);
    }

    TEST(VulkanFramePolicyTest, ATimedOutFenceWaitAndEveryOtherErrorSkipTheFrame) {
        EXPECT_EQ(StepAfter(VK_TIMEOUT), FrameStep::Skip);
        EXPECT_EQ(StepAfter(VK_ERROR_OUT_OF_HOST_MEMORY), FrameStep::Skip);
        EXPECT_EQ(StepAfter(VK_ERROR_OUT_OF_DEVICE_MEMORY), FrameStep::Skip);
        EXPECT_EQ(StepAfter(VK_ERROR_UNKNOWN), FrameStep::Skip);
    }

    TEST(VulkanFramePolicyTest, DeviceLostIsTerminal) {
        static_assert(StepAfter(VK_ERROR_DEVICE_LOST) == FrameStep::DeviceLost);
        EXPECT_EQ(StepAfter(VK_ERROR_DEVICE_LOST), FrameStep::DeviceLost);
    }

    TEST(VulkanFramePolicyTest, AFailureStreakIsLoggedOncePerCause) {
        EXPECT_TRUE(IsNewFailure(VK_SUCCESS, VK_TIMEOUT));
        EXPECT_FALSE(IsNewFailure(VK_TIMEOUT, VK_TIMEOUT));
        EXPECT_TRUE(IsNewFailure(VK_TIMEOUT, VK_ERROR_DEVICE_LOST));
    }

    TEST(VulkanFramePolicyTest, AClearColorIsTakenByOnePassAndThenItIsOpaqueBlack) {
        ClearColorSlot slot;
        const VkClearColorValue untouched = slot.Take();
        EXPECT_EQ(untouched.float32[0], 0.0f);
        EXPECT_EQ(untouched.float32[3], 1.0f);

        slot.Set({{0.25f, 0.5f, 0.75f, 0.5f}});
        const VkClearColorValue first = slot.Take();
        EXPECT_EQ(first.float32[0], 0.25f);
        EXPECT_EQ(first.float32[1], 0.5f);
        EXPECT_EQ(first.float32[2], 0.75f);
        EXPECT_EQ(first.float32[3], 0.5f);

        const VkClearColorValue second = slot.Take();
        EXPECT_EQ(second.float32[0], 0.0f);
        EXPECT_EQ(second.float32[1], 0.0f);
        EXPECT_EQ(second.float32[2], 0.0f);
        EXPECT_EQ(second.float32[3], 1.0f);
    }

    TEST(VulkanFramePolicyTest, TheLastClearBeforeAPassWins) {
        ClearColorSlot slot;
        slot.Set({{1.0f, 0.0f, 0.0f, 1.0f}});
        slot.Set({{0.0f, 1.0f, 0.0f, 1.0f}});
        EXPECT_EQ(slot.Take().float32[1], 1.0f);
    }

}  // namespace BECore::Tests
