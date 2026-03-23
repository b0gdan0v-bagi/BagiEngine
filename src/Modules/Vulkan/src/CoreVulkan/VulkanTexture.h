#pragma once

#include <BECore/Renderer/ITexture.h>
#include <vulkan/vulkan.h>

namespace BECore {

    /**
     * Vulkan implementation of ITexture.
     *
     * Holds the GPU-side image resources:
     *   VkImage / VkDeviceMemory  — device-local image in SHADER_READ_ONLY layout
     *   VkImageView               — 2D view over the full image
     *   VkSampler                 — LINEAR filter, CLAMP_TO_EDGE
     *   VkDescriptorSet           — pre-bound combined-image-sampler set (set 0)
     *
     * All resources are destroyed in the destructor; the caller must ensure the
     * GPU is idle before the last reference is released.
     */
    class VulkanTexture : public ITexture {
        BE_CLASS(VulkanTexture)
    public:
        VulkanTexture() = default;
        ~VulkanTexture() override;

        ResourceState GetState() const override;
        PoolString GetPath() const override;
        uint64_t GetMemoryUsage() const override;
        PoolString GetTypeName() const override;

        float GetWidth() const override;
        float GetHeight() const override;

        VkImageView GetImageView() const {
            return _imageView;
        }
        VkSampler GetSampler() const {
            return _sampler;
        }
        VkDescriptorSet GetDescriptorSet() const {
            return _descriptorSet;
        }

    private:
        friend class VulkanTextureLoader;

        void SetLoaded(PoolString path, VkDevice device, VkDescriptorPool descriptorPool, VkImage image, VkDeviceMemory memory, VkImageView imageView, VkSampler sampler, VkDescriptorSet descriptorSet,
                       float width, float height);
        void SetFailed(PoolString path);

        VkDevice _device = VK_NULL_HANDLE;
        VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;

        VkImage _image = VK_NULL_HANDLE;
        VkDeviceMemory _memory = VK_NULL_HANDLE;
        VkImageView _imageView = VK_NULL_HANDLE;
        VkSampler _sampler = VK_NULL_HANDLE;
        VkDescriptorSet _descriptorSet = VK_NULL_HANDLE;

        PoolString _path;
        ResourceState _state = ResourceState::Unloaded;
        float _width = 0.0f;
        float _height = 0.0f;
    };

}  // namespace BECore
