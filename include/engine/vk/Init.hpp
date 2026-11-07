/*
$$\    $$\ $$\   $$\   Vulkan helper functionality.
$$ |   $$ |$$ | $$  |  Copyright (c) 2026 Nikita Martynau 
$$ |   $$ |$$ |$$  /   https://opensource.org/license/mit 
\$$\  $$  |$$$$$  /    insert git repo url here
 \$$\$$  / $$  $$<     
  \$$$  /  $$ |\$$\    Convenience function to init vulkan painlessly
   \$  /   $$ | \$$\   using single easy-to-fill struct.
    \_/    \__|  \__|  Swapchain creation utility.
*/
#pragma once
#include "vulkan.h"

#include <unordered_set>
#include <vector>
#include <map>
#include <optional>
#include "engine/dsa/ECS.hpp" // SparseSet

namespace egn::vk {

/// @brief Creates The instance, device, allocator.
struct InstanceCreateInfo {
    /// The name of the application.
    std::string appName; 

    /// Vulkan api version,
    uint32_t version = VK_API_VERSION_1_3; 
    
    /// A list of required layers.
    std::vector<char const *> layers; 

    /// A list of required instance extensions.
    std::vector<char const *> instanceExtensions; 

    VkDebugUtilsMessageSeverityFlagsEXT messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

    /// Leave VK_NULL_HANDLE for default callback.
    PFN_vkDebugUtilsMessengerCallbackEXT debugCallbackOverride = VK_NULL_HANDLE; 
};
struct DeviceCreateInfo {
    /// The surface handle. Leave null for offscreen rendering.
    VkSurfaceKHR surface = VK_NULL_HANDLE; 

    /// A list of required device extensions. VK_KHR_SWAPCHAIN_EXTENSION_NAME is implicitly included if offscreen is not true.
    std::vector<char const *> deviceExtensions;

    /// A list of required queues. Present queue is searched for implicitly.
    std::vector<VkQueueFlagBits> queues = { VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT }; 

    /// Required device features. No need to set sType of pNext
    struct DeviceFeatures {
        VkPhysicalDeviceFeatures         features;
        VkPhysicalDeviceVulkan11Features vulkan11;
        VkPhysicalDeviceVulkan12Features vulkan12;
        VkPhysicalDeviceVulkan13Features vulkan13;
        VkPhysicalDeviceVulkan14Features vulkan14;
    } deviceFeatures; 

    /// The allocator create flags.
    VmaAllocatorCreateFlags allocatorFlags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; 
};

/// @brief Add necessary extensions and layers to enable validation layers.
inline void enableValidationLayers(InstanceCreateInfo &info) {
    info.layers.emplace_back("VK_LAYER_KHRONOS_validation");
    info.instanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
}

struct QueueFamilies {
    VkDevice device;
    std::unordered_map<VkQueueFlagBits, uint32_t> indices;
    std::optional<uint32_t> presentQueue;

    SparseSet<VkDeviceQueueCreateInfo> deviceCreateInfo;
    std::unordered_set<uint32_t> uniqueFamilies;
    uint32_t count = 0;

    /// @brief Get the queue from a queue family
    /// @returns VK_NULL_HANDLE if queue type is not present, a valid queue otherwise
    VkQueue getQueue(VkQueueFlagBits type, uint32_t queueIndex = 0) const;
};
struct Instance {
    uint32_t version = VK_API_VERSION_1_3; 
    bool valid = false;

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    std::vector<char const *> enabledInstanceExtensions; /// A list of enabled instance extensions.
    std::vector<char const *> enabledLayers; /// A list of enabled layers.
};
struct Device {
    bool valid = false; /// Indicates that the initialization went successfully.

    std::vector<char const *> enabledDeviceExtensions; /// A list of enabled device extensions.
    QueueFamilies queueFamilies;

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator vma = VK_NULL_HANDLE;
};

Instance createInstance(InstanceCreateInfo info);
Device createDevice(Instance const &instance, DeviceCreateInfo info);

struct SwapchainCreateInfo {
    struct AllocateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
    } alloc;
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkExtent2D size = {0, 0};
    VkImageUsageFlags imageUsage = 0;
    Registry *registry = nullptr;
};
struct Swapchain {
    VkSwapchainKHR swapchain;

    std::vector<Entity> images;

    VkSwapchainCreateInfoKHR createInfo;
    VkSurfaceCapabilitiesKHR capabilities;

    SwapchainCreateInfo::AllocateInfo alloc;
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
};
Swapchain makeSwapchain(SwapchainCreateInfo const &ci);
void resizeSwapchain(Swapchain &swapchain, VkExtent2D size);
void destroy(Swapchain &pipeline);


} // namespace vk