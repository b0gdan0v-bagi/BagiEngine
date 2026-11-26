#include "VulkanGpuTimer.h"

#include <BECore/Logger/LogEvent.h>
#include <CoreVulkan/VulkanDevice.h>

namespace BECore {

    namespace {

        constexpr uint32_t kQueriesPerSlot = 2;
        constexpr uint32_t kBeginQuery = 0;
        constexpr uint32_t kEndQuery = 1;

        constexpr uint32_t FirstQuery(uint32_t slot) {
            return slot * kQueriesPerSlot;
        }

    }  // namespace

    void VulkanGpuTimer::Initialize(const VulkanDevice& device, uint32_t slots) {
        const uint32_t validBits = device.GetTimestampValidBits();
        const float periodNs = device.ReadTimestampPeriod();
        if (!VulkanTimestampPolicy::CanTime(validBits, periodNs)) {
            LOG_INFO("VulkanGpuTimer: no GPU time on this device ({} valid timestamp bits, a tick of {} ns)", validBits, periodNs);
            return;
        }

        VkQueryPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        poolInfo.queryCount = slots * kQueriesPerSlot;
        const VkResult result = vkCreateQueryPool(device.GetDevice(), &poolInfo, nullptr, &_pool);
        if (result != VK_SUCCESS) {
            _pool = VK_NULL_HANDLE;
            LOG_INFO("VulkanGpuTimer: no GPU time, the query pool cannot be made (VkResult {})", static_cast<int>(result));
            return;
        }
        _validBits = validBits;
        _submitted.assign(slots, false);
    }

    void VulkanGpuTimer::Destroy(VkDevice device) {
        if (_pool == VK_NULL_HANDLE) {
            return;
        }
        vkDestroyQueryPool(device, _pool, nullptr);
        _pool = VK_NULL_HANDLE;
        _submitted.clear();
        _lastTaken.reset();
    }

    void VulkanGpuTimer::BeginFrame(VkCommandBuffer cmd, uint32_t slot) {
        if (_pool == VK_NULL_HANDLE) {
            return;
        }
        vkCmdResetQueryPool(cmd, _pool, FirstQuery(slot), kQueriesPerSlot);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, _pool, FirstQuery(slot) + kBeginQuery);
    }

    void VulkanGpuTimer::EndFrame(VkCommandBuffer cmd, uint32_t slot) {
        if (_pool == VK_NULL_HANDLE) {
            return;
        }
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, _pool, FirstQuery(slot) + kEndQuery);
    }

    void VulkanGpuTimer::MarkSubmitted(uint32_t slot) {
        if (_pool != VK_NULL_HANDLE) {
            _submitted[slot] = true;
        }
    }

    Optional<double> VulkanGpuTimer::TakeResult(const VulkanDevice& device, uint32_t slot) {
        if (_pool == VK_NULL_HANDLE || !_submitted[slot]) {
            return std::nullopt;
        }
        _submitted[slot] = false;
        Array<uint64_t, kQueriesPerSlot> stamps{};
        // No wait flag: VK_NOT_READY is a frame without a value, never a stall
        const VkResult result = vkGetQueryPoolResults(device.GetDevice(), _pool, FirstQuery(slot), kQueriesPerSlot, sizeof(stamps), stamps.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
        if (result != VK_SUCCESS) {
            return std::nullopt;
        }
        const VulkanTimestampPolicy::TimestampSpan span{stamps[kBeginQuery], stamps[kEndQuery]};
        if (!VulkanTimestampPolicy::IsNewSpan(_lastTaken ? &*_lastTaken : nullptr, span, _validBits)) {
            return std::nullopt;
        }
        _lastTaken = span;
        return VulkanTimestampPolicy::ElapsedMs(span.begin, span.end, _validBits, device.ReadTimestampPeriod());
    }

}  // namespace BECore
