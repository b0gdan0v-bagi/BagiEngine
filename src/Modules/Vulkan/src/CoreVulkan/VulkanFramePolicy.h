#pragma once

#include <BECore/Utils/EnumUtils.h>
#include <cstdint>
#include <vulkan/vulkan.h>

/**
 * What a frame does about the result of the Vulkan calls it makes itself (wait for the frame slot's fence, begin and
 * end the command buffer, submit) as pure functions. Header only and free of any Vulkan call, so a test checks them
 * without a device (`tests/BECore/VulkanFramePolicyTest.cpp`). The results of acquire and present are
 * `VulkanSwapchainPolicy`'s.
 */
namespace BECore::VulkanFramePolicy {

    /// How long a frame waits for the GPU to finish the previous use of its slot. Finite, so a submit that never
    /// signaled its fence costs a few skipped frames and a log line instead of a frozen program
    constexpr uint64_t kFenceTimeoutNs = 5'000'000'000;
    static_assert(kFenceTimeoutNs != UINT64_MAX, "an infinite wait is what the timeout exists to avoid");

    /// What the frame does after a call: Proceed (the call worked), Skip (the frame records and submits nothing; the
    /// next one tries again), DeviceLost (terminal: the device will not come back, every later frame is skipped)
    CORE_ENUM(FrameStep, uint8_t, Proceed, Skip, DeviceLost)

    /// After vkWaitForFences, vkBeginCommandBuffer, vkEndCommandBuffer, vkResetFences or vkQueueSubmit. A fence wait
    /// that timed out means the GPU did not finish in time, and any other error is not recoverable inside the
    /// frame, so both skip it
    [[nodiscard]] constexpr FrameStep StepAfter(VkResult result) {
        switch (result) {
            case VK_SUCCESS:
                return FrameStep::Proceed;
            case VK_ERROR_DEVICE_LOST:
                return FrameStep::DeviceLost;
            default:
                return FrameStep::Skip;
        }
    }

    /// True when a failure is worth a log line: the first of a streak, or another cause than the one logged last
    /// (`lastLogged` is VK_SUCCESS when no failure streak is going on). A frame that fails every 16 ms must not log
    /// at 60 Hz
    [[nodiscard]] constexpr bool IsNewFailure(VkResult lastLogged, VkResult failure) {
        return failure != lastLogged;
    }

    /// The colour `IRenderer::Clear()` named for the next pass. `Take()` gives it to the pass that opens and goes back
    /// to opaque black, so a clear made for an offscreen target does not tint a later pass of the swapchain
    class ClearColorSlot {
    public:
        void Set(const VkClearColorValue& color) {
            _color = color;
            _isSet = true;
        }

        [[nodiscard]] VkClearColorValue Take() {
            const VkClearColorValue taken = _isSet ? _color : OpaqueBlack();
            _isSet = false;
            return taken;
        }

    private:
        static VkClearColorValue OpaqueBlack() {
            return {{0.0f, 0.0f, 0.0f, 1.0f}};
        }

        VkClearColorValue _color = OpaqueBlack();
        bool _isSet = false;
    };

}  // namespace BECore::VulkanFramePolicy
