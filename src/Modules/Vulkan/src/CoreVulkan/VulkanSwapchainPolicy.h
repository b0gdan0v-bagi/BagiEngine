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
 * rebuilt, which extent and surface format to ask for, how ImGui's colours meet the swapchain. Header only and free of any Vulkan call, so a test
 * checks them without a device (`tests/BECore/VulkanSwapchainPolicyTest.cpp`).
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

    /// Where ImGui's vertex colours go: Direct (the swapchain is not sRGB, the colours are written as they are, the
    /// stock shader), Linearize (the swapchain is sRGB, the vertex shader turns the colours into linear values)
    CORE_ENUM(ImGuiColorMode, uint8_t, Direct, Linearize)

    /// Where a swapchain status comes from: the result of vkAcquireNextImageKHR or of vkQueuePresentKHR
    CORE_ENUM(SwapchainStage, uint8_t, Acquire, Present)

    /// What the frame does about a status (ActionAfter):
    /// Proceed (nothing to do), ProceedAndRecreateLater (the frame goes on, the swapchain is rebuilt before the next
    /// one), RecreateAndRetry (rebuild now and acquire again; the frame has not begun), Fail (log, skip the frame)
    CORE_ENUM(SwapchainAction, uint8_t, Proceed, ProceedAndRecreateLater, RecreateAndRetry, Fail)

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

    /// What to do about the status of an acquire or a present. A suboptimal acquire still signaled its semaphore, so
    /// the frame must go on and be submitted; an out-of-date acquire signaled nothing, so the frame has not begun and
    /// the swapchain is rebuilt at once; a present that is out of date or suboptimal comes after the submit, so the
    /// rebuild waits for the next frame.
    [[nodiscard]] constexpr SwapchainAction ActionAfter(SwapchainStage stage, SwapchainStatus status) {
        switch (status) {
            case SwapchainStatus::Ok:
                return SwapchainAction::Proceed;
            case SwapchainStatus::Suboptimal:
                return SwapchainAction::ProceedAndRecreateLater;
            case SwapchainStatus::OutOfDate:
                return stage == SwapchainStage::Acquire ? SwapchainAction::RecreateAndRetry : SwapchainAction::ProceedAndRecreateLater;
            default:
                return SwapchainAction::Fail;
        }
    }

    /// True when the swapchain must be rebuilt before the next frame: the window's pixel size differs from
    /// `requested`, or something (a window event, a suboptimal result) marked it dirty. Never while minimized:
    /// there is nothing to build. `requested` is the pixel size the swapchain was last created for, not the extent the
    /// surface gave back: a surface may report another fixed extent (a driver that lags behind a drag, a rounded
    /// fractional scale), and comparing with that would rebuild the swapchain on every frame.
    [[nodiscard]] constexpr bool NeedsRecreate(VkExtent2D requested, int pixelWidth, int pixelHeight, bool dirty) {
        if (IsMinimized(pixelWidth, pixelHeight)) {
            return false;
        }
        return dirty || requested.width != static_cast<uint32_t>(pixelWidth) || requested.height != static_cast<uint32_t>(pixelHeight);
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

    /// True for the formats whose attachment encodes linear values to sRGB on write (and whose sampler decodes on
    /// read): the `*_SRGB` formats a swapchain can have
    [[nodiscard]] constexpr bool IsSrgbFormat(VkFormat format) {
        switch (format) {
            case VK_FORMAT_B8G8R8A8_SRGB:
            case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
            case VK_FORMAT_R8G8B8_SRGB:
            case VK_FORMAT_B8G8R8_SRGB:
                return true;
            default:
                return false;
        }
    }

    /// How ImGui's colours have to be written into the main swapchain. ImGui's vertex colours are sRGB values (a
    /// theme colour 0.14 means "0.14 on the screen"), but an sRGB attachment encodes what it is given, so written as
    /// they are they come out much too light: they have to be turned into linear values first. A swapchain that is
    /// not sRGB stores them as they are.
    [[nodiscard]] constexpr ImGuiColorMode ChooseImGuiColorMode(VkFormat mainSwapchainFormat) {
        return IsSrgbFormat(mainSwapchainFormat) ? ImGuiColorMode::Linearize : ImGuiColorMode::Direct;
    }

    /// The format the swapchains of ImGui's secondary (floating) windows must ask for. The backend builds one
    /// shader pair for the main window and for them, so they must end in the same colour pipeline: sRGB when the
    /// vertex shader linearizes, otherwise UNORM, the format the backend picks by itself.
    [[nodiscard]] constexpr VkFormat ImGuiViewportFormat(ImGuiColorMode mode) {
        return mode == ImGuiColorMode::Linearize ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
    }

}  // namespace BECore::VulkanSwapchainPolicy
