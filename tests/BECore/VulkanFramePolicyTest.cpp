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

}  // namespace BECore::Tests
