#include "VulkanGeometryBuffer.h"

#include <BECore/Logger/LogEvent.h>
#include <CoreVulkan/VulkanDevice.h>

namespace BECore {

    namespace {

        // A new buffer is this big unless one draw needs more: RmlUi's biggest screens use a fraction of it per frame
        constexpr VkDeviceSize kDefaultChunkBytes = 1024 * 1024;
        // Vertices are 20 bytes and indices 4, so every piece starts on a multiple of 4
        constexpr VkDeviceSize kAlignment = 4;

        VkDeviceSize AlignUp(VkDeviceSize value) {
            return (value + kAlignment - 1) / kAlignment * kAlignment;
        }

    }  // namespace

    bool VulkanGeometryBuffer::Initialize(const VulkanDevice& device, uint32_t framesInFlight) {
        _device = &device;
        _slots.resize(framesInFlight);
        return true;
    }

    void VulkanGeometryBuffer::Destroy(VkDevice device) {
        for (FrameSlot& slot : _slots) {
            for (Chunk& chunk : slot.chunks) {
                if (chunk.mapped != nullptr) {
                    vkUnmapMemory(device, chunk.memory);
                }
                if (chunk.buffer != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, chunk.buffer, nullptr);
                }
                if (chunk.memory != VK_NULL_HANDLE) {
                    vkFreeMemory(device, chunk.memory, nullptr);
                }
            }
        }
        _slots.clear();
        _device = nullptr;
    }

    void VulkanGeometryBuffer::BeginFrame(uint32_t frameIndex) {
        ASSERT(frameIndex < _slots.size());
        FrameSlot& slot = _slots[frameIndex];
        for (Chunk& chunk : slot.chunks) {
            chunk.used = 0;
        }
        slot.current = 0;
    }

    bool VulkanGeometryBuffer::Allocate(uint32_t frameIndex, VkDeviceSize vertexBytes, VkDeviceSize indexBytes, Allocation& allocation) {
        ASSERT(frameIndex < _slots.size());
        FrameSlot& slot = _slots[frameIndex];

        const VkDeviceSize vertexSize = AlignUp(vertexBytes);
        const VkDeviceSize needed = vertexSize + AlignUp(indexBytes);

        // Move on to the first chunk with room; a chunk that cannot take this piece stays full for the rest of the frame
        while (slot.current < slot.chunks.size() && slot.chunks[slot.current].capacity - slot.chunks[slot.current].used < needed) {
            ++slot.current;
        }
        if (slot.current == slot.chunks.size() && !CreateChunk(slot, needed)) {
            return false;
        }

        Chunk& chunk = slot.chunks[slot.current];
        auto* base = static_cast<uint8_t*>(chunk.mapped);
        allocation.buffer = chunk.buffer;
        allocation.vertexOffset = chunk.used;
        allocation.indexOffset = chunk.used + vertexSize;
        allocation.vertices = base + allocation.vertexOffset;
        allocation.indices = base + allocation.indexOffset;
        chunk.used += needed;
        return true;
    }

    VkDeviceSize VulkanGeometryBuffer::GetCapacity() const {
        VkDeviceSize total = 0;
        for (const FrameSlot& slot : _slots) {
            for (const Chunk& chunk : slot.chunks) {
                total += chunk.capacity;
            }
        }
        return total;
    }

    bool VulkanGeometryBuffer::CreateChunk(FrameSlot& slot, VkDeviceSize minCapacity) {
        const VkDevice device = _device->GetDevice();
        Chunk chunk;
        chunk.capacity = minCapacity > kDefaultChunkBytes ? minCapacity : kDefaultChunkBytes;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = chunk.capacity;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &chunk.buffer) != VK_SUCCESS) {
            LOG_ERROR("VulkanGeometryBuffer: failed to create a buffer");
            return false;
        }

        VkMemoryRequirements memReqs{};
        vkGetBufferMemoryRequirements(device, chunk.buffer, &memReqs);
        const uint32_t memType = _device->FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (memType == UINT32_MAX) {
            LOG_ERROR("VulkanGeometryBuffer: no host-visible, host-coherent memory type");
            vkDestroyBuffer(device, chunk.buffer, nullptr);
            return false;
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = memType;
        if (vkAllocateMemory(device, &allocInfo, nullptr, &chunk.memory) != VK_SUCCESS) {
            LOG_ERROR("VulkanGeometryBuffer: failed to allocate memory for a buffer");
            vkDestroyBuffer(device, chunk.buffer, nullptr);
            return false;
        }

        if (vkBindBufferMemory(device, chunk.buffer, chunk.memory, 0) != VK_SUCCESS || vkMapMemory(device, chunk.memory, 0, chunk.capacity, 0, &chunk.mapped) != VK_SUCCESS) {
            LOG_ERROR("VulkanGeometryBuffer: failed to bind or map a buffer");
            vkDestroyBuffer(device, chunk.buffer, nullptr);
            vkFreeMemory(device, chunk.memory, nullptr);
            return false;
        }

        slot.chunks.push_back(chunk);
        return true;
    }

}  // namespace BECore
