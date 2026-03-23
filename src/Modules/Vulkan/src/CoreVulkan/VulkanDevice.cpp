#include "VulkanDevice.h"

#include <BECore/Logger/LogEvent.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <SDL3/SDL_vulkan.h>

namespace BECore {

    // Required device extensions for swapchain support
    static constexpr const char* kDeviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    bool VulkanDevice::InitializeInstance(SDL_Window* window) {
        if (!CreateInstance(window)) {
            LOG_ERROR("Vulkan: Failed to create VkInstance");
            return false;
        }
        return true;
    }

    bool VulkanDevice::InitializeDevice(VkSurfaceKHR surface) {
        if (!PickPhysicalDevice(surface)) {
            LOG_ERROR("Vulkan: No suitable GPU found");
            return false;
        }
        if (!CreateLogicalDevice()) {
            LOG_ERROR("Vulkan: Failed to create VkDevice");
            return false;
        }
        return true;
    }

    void VulkanDevice::Destroy() {
        if (_device != VK_NULL_HANDLE) {
            vkDestroyDevice(_device, nullptr);
            _device = VK_NULL_HANDLE;
        }
        if (_instance != VK_NULL_HANDLE) {
            vkDestroyInstance(_instance, nullptr);
            _instance = VK_NULL_HANDLE;
        }
        _physicalDevice = VK_NULL_HANDLE;
    }

    bool VulkanDevice::CreateInstance(SDL_Window* window) {
        // Collect extensions required by SDL
        uint32_t sdlExtCount = 0;
        const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&sdlExtCount);
        if (!sdlExts) {
            return false;
        }

        eastl::vector<const char*> extensions(sdlExts, sdlExts + sdlExtCount);

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "BagiEngine";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "BagiEngine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        createInfo.enabledLayerCount = 0;

        return vkCreateInstance(&createInfo, nullptr, &_instance) == VK_SUCCESS;
    }

    bool VulkanDevice::PickPhysicalDevice(VkSurfaceKHR surface) {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(_instance, &count, nullptr);
        if (count == 0) {
            return false;
        }

        eastl::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(_instance, &count, devices.data());

        // Prefer discrete GPU; fall back to the first suitable device
        VkPhysicalDevice fallback = VK_NULL_HANDLE;
        for (VkPhysicalDevice device : devices) {
            if (!IsDeviceSuitable(device, surface)) {
                continue;
            }
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(device, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                _physicalDevice = device;
                _queueFamilies = FindQueueFamilies(device, surface);
                LOG_INFO(Format("Vulkan: Selected discrete GPU: {}", props.deviceName));
                return true;
            }
            if (fallback == VK_NULL_HANDLE) {
                fallback = device;
                _queueFamilies = FindQueueFamilies(device, surface);
            }
        }

        if (fallback != VK_NULL_HANDLE) {
            _physicalDevice = fallback;
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(_physicalDevice, &props);
            LOG_INFO(Format("Vulkan: Selected GPU: {}", props.deviceName));
            return true;
        }
        return false;
    }

    bool VulkanDevice::CreateLogicalDevice() {
        const uint32_t gfx = _queueFamilies.graphicsFamily;
        const uint32_t present = _queueFamilies.presentFamily;

        eastl::vector<VkDeviceQueueCreateInfo> queueInfos;
        const float priority = 1.0f;

        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = gfx;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        queueInfos.push_back(queueInfo);

        // Add separate present queue only if it differs from graphics queue
        if (present != gfx) {
            queueInfo.queueFamilyIndex = present;
            queueInfos.push_back(queueInfo);
        }

        VkPhysicalDeviceFeatures features{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
        createInfo.pQueueCreateInfos = queueInfos.data();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(std::size(kDeviceExtensions));
        createInfo.ppEnabledExtensionNames = kDeviceExtensions;
        createInfo.pEnabledFeatures = &features;

        if (vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device) != VK_SUCCESS) {
            return false;
        }

        vkGetDeviceQueue(_device, gfx, 0, &_graphicsQueue);
        vkGetDeviceQueue(_device, present, 0, &_presentQueue);
        return true;
    }

    VulkanQueueFamilyIndices VulkanDevice::FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const {

        VulkanQueueFamilyIndices indices;

        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        eastl::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

        for (uint32_t i = 0; i < count; ++i) {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
            }

            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if (presentSupport) {
                indices.presentFamily = i;
            }

            if (indices.IsComplete()) {
                break;
            }
        }
        return indices;
    }

    bool VulkanDevice::IsDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) const {
        // Check queue families
        if (!FindQueueFamilies(device, surface).IsComplete()) {
            return false;
        }

        // Check required extensions
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr);
        eastl::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, available.data());

        for (const char* required : kDeviceExtensions) {
            bool found = false;
            for (const auto& ext : available) {
                if (strcmp(required, ext.extensionName) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }

        // Check swapchain capabilities
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &caps);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

        return formatCount > 0 && presentModeCount > 0;
    }

    uint32_t VulkanDevice::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memProps);

        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    VkCommandBuffer VulkanDevice::BeginSingleTimeCommands(VkCommandPool pool) const {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = pool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(_device, &allocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        return cmd;
    }

    void VulkanDevice::EndSingleTimeCommands(VkCommandPool pool, VkCommandBuffer cmd) const {
        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        vkQueueSubmit(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(_graphicsQueue);

        vkFreeCommandBuffers(_device, pool, 1, &cmd);
    }

}  // namespace BECore
