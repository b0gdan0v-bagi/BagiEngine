#pragma once

#include <EASTL/vector.h>
#include <vulkan/vulkan.h>

namespace BECore {

    class VulkanDevice;

    /**
     * Vertex and index memory for the draws of one frame, one set per frame in flight.
     *
     * A frame slot owns a list of host-visible, host-coherent buffers that stay mapped. Allocate() hands out a piece of
     * the current buffer (vertices first, then indices, both 4-byte aligned) and adds a bigger buffer when the piece does
     * not fit, so a draw already recorded never points at memory that moved. BeginFrame() of a slot rewinds it; call it
     * only after the fence of that slot has been waited on, since the GPU may still read the slot's data until then.
     * The buffers are kept for later frames, so a steady scene allocates nothing after the first frames.
     *
     * Main thread only, like every GPU call.
     */
    class VulkanGeometryBuffer {
    public:
        /// A piece of one buffer, written by the caller
        struct Allocation {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceSize vertexOffset = 0;
            VkDeviceSize indexOffset = 0;
            void* vertices = nullptr;
            void* indices = nullptr;
        };

        VulkanGeometryBuffer() = default;
        ~VulkanGeometryBuffer() = default;

        VulkanGeometryBuffer(const VulkanGeometryBuffer&) = delete;
        VulkanGeometryBuffer& operator=(const VulkanGeometryBuffer&) = delete;

        bool Initialize(const VulkanDevice& device, uint32_t framesInFlight);
        /// The GPU must be idle
        void Destroy(VkDevice device);

        void BeginFrame(uint32_t frameIndex);

        /// False when the memory cannot be had (logged); the draw is then skipped
        bool Allocate(uint32_t frameIndex, VkDeviceSize vertexBytes, VkDeviceSize indexBytes, Allocation& allocation);

        /// The total size of the buffers made so far, in bytes (a test and the log use it)
        [[nodiscard]] VkDeviceSize GetCapacity() const;

    private:
        struct Chunk {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            void* mapped = nullptr;
            VkDeviceSize capacity = 0;
            VkDeviceSize used = 0;
        };

        struct FrameSlot {
            eastl::vector<Chunk> chunks;
            // The chunk the next piece comes from; the ones before it are full for this frame
            size_t current = 0;
        };

        bool CreateChunk(FrameSlot& slot, VkDeviceSize minCapacity);

        const VulkanDevice* _device = nullptr;
        eastl::vector<FrameSlot> _slots;
    };

}  // namespace BECore
