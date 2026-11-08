#pragma once

#include <BECore/Utils/EnumUtils.h>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vulkan/vulkan.h>

/**
 * The decisions around a swapchain as pure functions: what a Vulkan result means, when the swapchain has to be
 * rebuilt, which extent and surface format to ask for. Header only and free of any Vulkan call, so a test checks
 * them without a device (`tests/BECore/VulkanSwapchainPolicyTest.cpp`).
 */
namespace BECore::VulkanSwapchainPolicy {

    /// What the result of vkAcquireNextImageKHR / vkQueuePresentKHR means for the frame:
    /// Ok (success), Suboptimal (the call worked but the swapchain no longer matches the surface; recreate soon),
    /// OutOfDate (the swapchain cannot be used any more; recreate it now), Failed (any other error: device lost,
    /// surface lost, out of memory, ...)
    CORE_ENUM(SwapchainStatus, uint8_t, Ok, Suboptimal, OutOfDate, Failed)

    /// What VulkanSwapchain::Recreate did:
    /// Done (the swapchain, image views and framebuffers are new), ZeroExtent (the size is zero, minimized: nothing
    /// was touched), Failed (a Vulkan call failed or the surface no longer offers the format of the render pass; logged)
    CORE_ENUM(SwapchainRecreateResult, uint8_t, Done, ZeroExtent, Failed)

    /// The one place that turns a VkResult of acquire or present into a SwapchainStatus
    [[nodiscard]] constexpr SwapchainStatus ClassifyResult(VkResult result) {
        switch (result) {
            case VK_SUCCESS:
                return SwapchainStatus::Ok;
            case VK_SUBOPTIMAL_KHR:
                return SwapchainStatus::Suboptimal;
            case VK_ERROR_OUT_OF_DATE_KHR:
                return SwapchainStatus::OutOfDate;
            default:
                return SwapchainStatus::Failed;
        }
    }

    /// A window of zero pixels (minimized) has no surface to draw into
    [[nodiscard]] constexpr bool IsMinimized(int pixelWidth, int pixelHeight) {
        return pixelWidth <= 0 || pixelHeight <= 0;
    }

    /// True when the swapchain must be rebuilt before the next frame: the window's pixel size differs from the
    /// extent of the swapchain, or something (a window event, a suboptimal result) marked it dirty. Never while
    /// minimized: there is nothing to build.
    [[nodiscard]] constexpr bool NeedsRecreate(VkExtent2D current, int pixelWidth, int pixelHeight, bool dirty) {
        if (IsMinimized(pixelWidth, pixelHeight)) {
            return false;
        }
        return dirty || current.width != static_cast<uint32_t>(pixelWidth) || current.height != static_cast<uint32_t>(pixelHeight);
    }

    /// The surface decides when it reports a fixed current extent; otherwise (UINT32_MAX) the pixel size of the
    /// window, clamped to what the surface allows
    [[nodiscard]] constexpr VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& caps, int pixelWidth, int pixelHeight) {
        if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return caps.currentExtent;
        }
        VkExtent2D extent{static_cast<uint32_t>(std::max(pixelWidth, 0)), static_cast<uint32_t>(std::max(pixelHeight, 0))};
        extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        return extent;
    }

    /// The format to create the swapchain with. `keep` is the format of the swapchain being replaced: the render
    /// pass and the pipelines were built for it, so it is taken whenever the surface still offers it (with the sRGB
    /// non-linear color space). Otherwise BGRA8 sRGB, otherwise the first one. Empty for an empty list.
    [[nodiscard]] constexpr std::optional<VkSurfaceFormatKHR> ChooseSurfaceFormat(std::span<const VkSurfaceFormatKHR> formats, std::optional<VkFormat> keep = std::nullopt) {
        if (keep) {
            for (const VkSurfaceFormatKHR& f : formats) {
                if (f.format == *keep && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                    return f;
                }
            }
        }
        for (const VkSurfaceFormatKHR& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return f;
            }
        }
        if (formats.empty()) {
            return std::nullopt;
        }
        return formats.front();
    }

}  // namespace BECore::VulkanSwapchainPolicy
