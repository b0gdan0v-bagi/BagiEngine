#include "VulkanTexture.h"

#include <Generated/VulkanTexture.gen.hpp>

namespace BECore {

    VulkanTexture::~VulkanTexture() {
        if (_device == VK_NULL_HANDLE) {
            return;
        }

        if (_descriptorSet != VK_NULL_HANDLE && _descriptorPool != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(_device, _descriptorPool, 1, &_descriptorSet);
            _descriptorSet = VK_NULL_HANDLE;
        }
        if (_sampler != VK_NULL_HANDLE) {
            vkDestroySampler(_device, _sampler, nullptr);
            _sampler = VK_NULL_HANDLE;
        }
        if (_imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(_device, _imageView, nullptr);
            _imageView = VK_NULL_HANDLE;
        }
        if (_image != VK_NULL_HANDLE) {
            vkDestroyImage(_device, _image, nullptr);
            _image = VK_NULL_HANDLE;
        }
        if (_memory != VK_NULL_HANDLE) {
            vkFreeMemory(_device, _memory, nullptr);
            _memory = VK_NULL_HANDLE;
        }
    }

    ResourceState VulkanTexture::GetState() const {
        return _state;
    }

    PoolString VulkanTexture::GetPath() const {
        return _path;
    }

    uint64_t VulkanTexture::GetMemoryUsage() const {
        return static_cast<uint64_t>(_width * _height * 4);
    }

    PoolString VulkanTexture::GetTypeName() const {
        return "VulkanTexture"_intern;
    }

    float VulkanTexture::GetWidth() const {
        return _width;
    }

    float VulkanTexture::GetHeight() const {
        return _height;
    }

    void VulkanTexture::SetLoaded(PoolString path, VkDevice device, VkDescriptorPool descriptorPool, VkImage image, VkDeviceMemory memory, VkImageView imageView, VkSampler sampler,
                                  VkDescriptorSet descriptorSet, float width, float height) {
        _path = path;
        _device = device;
        _descriptorPool = descriptorPool;
        _image = image;
        _memory = memory;
        _imageView = imageView;
        _sampler = sampler;
        _descriptorSet = descriptorSet;
        _width = width;
        _height = height;
        _state = ResourceState::Loaded;
    }

    void VulkanTexture::SetFailed(PoolString path) {
        _path = path;
        _state = ResourceState::Failed;
    }

}  // namespace BECore
