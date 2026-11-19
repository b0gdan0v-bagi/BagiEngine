#pragma once

#include <EASTL/vector.h>
#include <vulkan/vulkan.h>

struct SDL_Window;

namespace BECore {

    struct VulkanQueueFamilyIndices {
        uint32_t graphicsFamily = UINT32_MAX;
        uint32_t presentFamily = UINT32_MAX;

        bool IsComplete() const {
            return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
        }
    };

    /**
     * Owns the VkInstance, the chosen VkPhysicalDevice, the VkDevice and its graphics and present queues.
     */
    class VulkanDevice {
    public:
        VulkanDevice() = default;
        ~VulkanDevice() = default;

        // Two steps because the surface needs an existing instance.
        bool InitializeInstance(SDL_Window* window);
        bool InitializeDevice(VkSurfaceKHR surface);
        void Destroy();

        VkInstance GetInstance() const {
            return _instance;
        }
        VkPhysicalDevice GetPhysicalDevice() const {
            return _physicalDevice;
        }
        VkDevice GetDevice() const {
            return _device;
        }
        VkQueue GetGraphicsQueue() const {
            return _graphicsQueue;
        }
        VkQueue GetPresentQueue() const {
            return _presentQueue;
        }

        const VulkanQueueFamilyIndices& GetQueueFamilies() const {
            return _queueFamilies;
        }

        // Memory helper: finds the index of a memory type satisfying both typeFilter and
        // required property flags. Returns UINT32_MAX if not found.
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

        // A recording command buffer of the pool, or VK_NULL_HANDLE (logged) when it could not be made or begun
        VkCommandBuffer BeginSingleTimeCommands(VkCommandPool pool) const;
        // Ends, submits and waits for `cmd`, then frees it. False (logged) when it was not recorded or submitted.
        [[nodiscard]] bool EndSingleTimeCommands(VkCommandPool pool, VkCommandBuffer cmd) const;

    private:
        bool CreateInstance(SDL_Window* window);
        bool PickPhysicalDevice(VkSurfaceKHR surface);
        bool CreateLogicalDevice();

        VulkanQueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const;
        bool IsDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) const;

        VkInstance _instance = VK_NULL_HANDLE;
        VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
        VkDevice _device = VK_NULL_HANDLE;
        VkQueue _graphicsQueue = VK_NULL_HANDLE;
        VkQueue _presentQueue = VK_NULL_HANDLE;
        VulkanQueueFamilyIndices _queueFamilies;
    };

}  // namespace BECore
