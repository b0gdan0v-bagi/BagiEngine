#include <CoreVulkan/VulkanSwapchainPolicy.h>
#include <array>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    using namespace VulkanSwapchainPolicy;

    namespace {

        constexpr uint32_t kUnset = std::numeric_limits<uint32_t>::max();

        VkSurfaceCapabilitiesKHR MakeCaps(VkExtent2D current, VkExtent2D minExtent, VkExtent2D maxExtent) {
            VkSurfaceCapabilitiesKHR caps{};
            caps.currentExtent = current;
            caps.minImageExtent = minExtent;
            caps.maxImageExtent = maxExtent;
            return caps;
        }

        VkSurfaceFormatKHR Format(VkFormat format, VkColorSpaceKHR space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return VkSurfaceFormatKHR{format, space};
        }

    }  // namespace

    TEST(VulkanSwapchainPolicyTest, ClassifyResultMapsTheResultsOfAcquireAndPresent) {
        static_assert(ClassifyResult(VK_SUCCESS) == SwapchainStatus::Ok);
        EXPECT_EQ(ClassifyResult(VK_SUCCESS), SwapchainStatus::Ok);
        EXPECT_EQ(ClassifyResult(VK_SUBOPTIMAL_KHR), SwapchainStatus::Suboptimal);
        EXPECT_EQ(ClassifyResult(VK_ERROR_OUT_OF_DATE_KHR), SwapchainStatus::OutOfDate);
        EXPECT_EQ(ClassifyResult(VK_ERROR_SURFACE_LOST_KHR), SwapchainStatus::Failed);
        EXPECT_EQ(ClassifyResult(VK_ERROR_DEVICE_LOST), SwapchainStatus::Failed);
        EXPECT_EQ(ClassifyResult(VK_ERROR_OUT_OF_HOST_MEMORY), SwapchainStatus::Failed);
        EXPECT_EQ(ClassifyResult(VK_NOT_READY), SwapchainStatus::Failed);
    }

    TEST(VulkanSwapchainPolicyTest, ActionAfterAcquire) {
        static_assert(ActionAfter(SwapchainStage::Acquire, SwapchainStatus::Ok) == SwapchainAction::Proceed);
        EXPECT_EQ(ActionAfter(SwapchainStage::Acquire, SwapchainStatus::Ok), SwapchainAction::Proceed);
        // Suboptimal signaled the semaphore: the frame goes on and is submitted
        EXPECT_EQ(ActionAfter(SwapchainStage::Acquire, SwapchainStatus::Suboptimal), SwapchainAction::ProceedAndRecreateLater);
        // Out of date signaled nothing: rebuild now and acquire again
        EXPECT_EQ(ActionAfter(SwapchainStage::Acquire, SwapchainStatus::OutOfDate), SwapchainAction::RecreateAndRetry);
        EXPECT_EQ(ActionAfter(SwapchainStage::Acquire, SwapchainStatus::Failed), SwapchainAction::Fail);
    }

    TEST(VulkanSwapchainPolicyTest, ActionAfterPresentNeverRetries) {
        EXPECT_EQ(ActionAfter(SwapchainStage::Present, SwapchainStatus::Ok), SwapchainAction::Proceed);
        // The frame is already submitted: the swapchain is rebuilt before the next one
        EXPECT_EQ(ActionAfter(SwapchainStage::Present, SwapchainStatus::Suboptimal), SwapchainAction::ProceedAndRecreateLater);
        EXPECT_EQ(ActionAfter(SwapchainStage::Present, SwapchainStatus::OutOfDate), SwapchainAction::ProceedAndRecreateLater);
        EXPECT_EQ(ActionAfter(SwapchainStage::Present, SwapchainStatus::Failed), SwapchainAction::Fail);
    }

    TEST(VulkanSwapchainPolicyTest, IsMinimizedWhenEitherSideIsZero) {
        static_assert(IsMinimized(0, 600) && !IsMinimized(800, 600));
        EXPECT_TRUE(IsMinimized(0, 0));
        EXPECT_TRUE(IsMinimized(0, 600));
        EXPECT_TRUE(IsMinimized(800, 0));
        EXPECT_FALSE(IsMinimized(1, 1));
        EXPECT_FALSE(IsMinimized(1600, 1200));
    }

    TEST(VulkanSwapchainPolicyTest, NeedsRecreateWhenTheSizeDiffers) {
        static_assert(!NeedsRecreate({800, 600}, 800, 600, false));
        const VkExtent2D current{800, 600};
        EXPECT_FALSE(NeedsRecreate(current, 800, 600, false));
        EXPECT_TRUE(NeedsRecreate(current, 801, 600, false));
        EXPECT_TRUE(NeedsRecreate(current, 800, 599, false));
        EXPECT_TRUE(NeedsRecreate(current, 1600, 1200, false));
    }

    TEST(VulkanSwapchainPolicyTest, NeedsRecreateComparesWithTheSizeAskedForNotTheSizeGiven) {
        // The swapchain was asked for 1000 x 700 and the surface gave 998 x 700: the window still has 1000 x 700,
        // so nothing is rebuilt (comparing with the 998 would rebuild on every frame)
        const VkExtent2D requested{1000, 700};
        EXPECT_FALSE(NeedsRecreate(requested, 1000, 700, false));
        EXPECT_TRUE(NeedsRecreate(requested, 1001, 700, false));
    }

    TEST(VulkanSwapchainPolicyTest, NeedsRecreateWhenDirtyEvenAtTheSameSize) {
        EXPECT_TRUE(NeedsRecreate({800, 600}, 800, 600, true));
    }

    TEST(VulkanSwapchainPolicyTest, NeverRecreatesWhileMinimized) {
        EXPECT_FALSE(NeedsRecreate({800, 600}, 0, 0, false));
        EXPECT_FALSE(NeedsRecreate({800, 600}, 0, 0, true));
        EXPECT_FALSE(NeedsRecreate({800, 600}, 800, 0, true));
        EXPECT_FALSE(NeedsRecreate({800, 600}, 0, 600, false));
    }

    TEST(VulkanSwapchainPolicyTest, ChooseExtentTakesTheFixedCurrentExtent) {
        const VkSurfaceCapabilitiesKHR caps = MakeCaps({640, 480}, {1, 1}, {4096, 4096});
        const VkExtent2D extent = ChooseExtent(caps, 1920, 1080);
        EXPECT_EQ(extent.width, 640u);
        EXPECT_EQ(extent.height, 480u);
    }

    TEST(VulkanSwapchainPolicyTest, ChooseExtentTakesThePixelSizeWhenTheSurfaceLeavesItOpen) {
        const VkSurfaceCapabilitiesKHR caps = MakeCaps({kUnset, kUnset}, {16, 16}, {4096, 2160});
        const VkExtent2D extent = ChooseExtent(caps, 1920, 1080);
        EXPECT_EQ(extent.width, 1920u);
        EXPECT_EQ(extent.height, 1080u);
    }

    TEST(VulkanSwapchainPolicyTest, ChooseExtentClampsToTheSurfaceLimits) {
        const VkSurfaceCapabilitiesKHR caps = MakeCaps({kUnset, kUnset}, {16, 32}, {4096, 2160});

        const VkExtent2D small = ChooseExtent(caps, 4, 8);
        EXPECT_EQ(small.width, 16u);
        EXPECT_EQ(small.height, 32u);

        const VkExtent2D large = ChooseExtent(caps, 8000, 5000);
        EXPECT_EQ(large.width, 4096u);
        EXPECT_EQ(large.height, 2160u);
    }

    TEST(VulkanSwapchainPolicyTest, ChooseSurfaceFormatPrefersTheKeptFormat) {
        const std::array formats{Format(VK_FORMAT_B8G8R8A8_SRGB), Format(VK_FORMAT_R8G8B8A8_SRGB), Format(VK_FORMAT_B8G8R8A8_UNORM)};

        const auto kept = ChooseSurfaceFormat(formats, VK_FORMAT_R8G8B8A8_SRGB);
        ASSERT_TRUE(kept.has_value());
        EXPECT_EQ(kept->format, VK_FORMAT_R8G8B8A8_SRGB);

        // Nothing to keep, or the kept format is not offered: BGRA8 sRGB
        EXPECT_EQ(ChooseSurfaceFormat(formats)->format, VK_FORMAT_B8G8R8A8_SRGB);
        EXPECT_EQ(ChooseSurfaceFormat(formats, VK_FORMAT_A2B10G10R10_UNORM_PACK32)->format, VK_FORMAT_B8G8R8A8_SRGB);
    }

    TEST(VulkanSwapchainPolicyTest, ChooseSurfaceFormatIgnoresAKeptFormatWithAnotherColorSpace) {
        const std::array formats{Format(VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT), Format(VK_FORMAT_B8G8R8A8_SRGB)};

        const auto chosen = ChooseSurfaceFormat(formats, VK_FORMAT_R8G8B8A8_SRGB);
        ASSERT_TRUE(chosen.has_value());
        EXPECT_EQ(chosen->format, VK_FORMAT_B8G8R8A8_SRGB);
        EXPECT_EQ(chosen->colorSpace, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
    }

    TEST(VulkanSwapchainPolicyTest, ChooseSurfaceFormatFallsBackToTheFirst) {
        const std::array formats{Format(VK_FORMAT_R8G8B8A8_UNORM), Format(VK_FORMAT_B8G8R8A8_UNORM)};

        const auto chosen = ChooseSurfaceFormat(formats);
        ASSERT_TRUE(chosen.has_value());
        EXPECT_EQ(chosen->format, VK_FORMAT_R8G8B8A8_UNORM);
    }

    TEST(VulkanSwapchainPolicyTest, ChooseSurfaceFormatOfAnEmptyListIsEmpty) {
        EXPECT_FALSE(ChooseSurfaceFormat({}).has_value());
        EXPECT_FALSE(ChooseSurfaceFormat({}, VK_FORMAT_B8G8R8A8_SRGB).has_value());
    }

}  // namespace BECore::Tests
