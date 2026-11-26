#pragma once

#include <BECore/Types/Types.h>
#include <CoreVulkan/VulkanTimestampPolicy.h>
#include <vulkan/vulkan.h>

namespace BECore {

    class VulkanDevice;

    /**
     * @brief The GPU time of a frame: a timestamp at the start of a frame slot's command buffer and one after its last render pass.
     *
     * A result is read after the slot's fence wait, so it is as many frames old as there are slots and a read never waits. The start is
     * written at the colour output stage, the stage the submit waits for the swapchain image at, so that wait is not in the span. On a device
     * that cannot time, every call does nothing.
     */
    class VulkanGpuTimer {
    public:
        /// Makes the queries of @p slots frames in flight. Logs one line and stays off when the device cannot time
        void Initialize(const VulkanDevice& device, uint32_t slots);
        void Destroy(VkDevice device);
        /// The first commands of the slot's command buffer, outside any render pass
        void BeginFrame(VkCommandBuffer cmd, uint32_t slot);
        /// After the last render pass of the slot's command buffer has ended
        void EndFrame(VkCommandBuffer cmd, uint32_t slot);
        /// The slot's command buffer was submitted, so its result is to be read
        void MarkSubmitted(uint32_t slot);
        /// The milliseconds of the slot's submitted frame, given once; call it after the slot's fence wait. Empty when the slot has no
        /// submitted frame, the driver has no result or what it gives is not a newer frame's (VulkanTimestampPolicy::IsNewSpan)
        [[nodiscard]] Optional<double> TakeResult(const VulkanDevice& device, uint32_t slot);

    private:
        VkQueryPool _pool = VK_NULL_HANDLE;
        Vector<bool> _submitted;  // per slot: a submitted frame whose result was not taken yet
        uint32_t _validBits = 0;
        Optional<VulkanTimestampPolicy::TimestampSpan> _lastTaken;
    };

}  // namespace BECore
