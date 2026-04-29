/*
$$\    $$\ $$\   $$\   My vulkan abstraction.
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

#include <vector>
#include <map>
#include <optional>
#include "ECS.hpp" // SparseSet

namespace vk {

/// Very minimal init options
/// The init utility is designed to be tweaked directly.
struct InitInfo
{
    std::string appName; ///< The name of the application.
    GLFWwindow *window = VK_NULL_HANDLE; ///< The window handle. 
    uint32_t version = VK_API_VERSION_1_3; ///< Vulkan api version,
    bool offscreen = false; ///< Controls whether presentation is required.
    
    std::vector<char const *> instanceExtensions; ///< A list of required instance extensions excluding required extensions.
    std::vector<char const *> deviceExtensions; ///< A list of required device extensions excluding required extensions. VK_KHR_SWAPCHAIN_EXTENSION_NAME is implicitly included if offscreen is not true.
    std::vector<char const *> layers; ///< A list of required layers.
    std::vector<VkQueueFlagBits> queues = { VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT }; ///< A list of required queues. Present queue is searched for implicitly.
    struct DeviceFeatures {
        VkPhysicalDeviceFeatures         features;
        VkPhysicalDeviceVulkan11Features vulkan11;
        VkPhysicalDeviceVulkan12Features vulkan12;
        VkPhysicalDeviceVulkan13Features vulkan13;
        VkPhysicalDeviceVulkan14Features vulkan14;
    } deviceFeatures; ///< Required device features. No need to set sType of pNext

    VmaAllocatorCreateFlags allocatorFlags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; ///< The allocator flags.

    VkDebugUtilsMessageSeverityFlagsEXT messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    PFN_vkDebugUtilsMessengerCallbackEXT debugCallbackOverride = VK_NULL_HANDLE; ///< Leave VK_NULL_HANDLE for default callback.
};

/// @brief Add necessary extensions and layers to enable validation layers.
inline void enableValidationLayers(InitInfo &info)
{
    info.layers.emplace_back("VK_LAYER_KHRONOS_validation");
    info.instanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
}

struct QueueFamilies
{
    VkDevice device;
    std::map<VkQueueFlagBits, uint32_t> indices;
    std::optional<uint32_t> presentQueue;

    SparseSet<VkDeviceQueueCreateInfo> deviceCreateInfo;
    SparseSet<uint32_t> uniqueFamilies;
    uint32_t count = 0;

    /// @brief Get the queue from a queue family
    /// @returns VK_NULL_HANDLE if queue type is not present, a valid queue otherwise
    VkQueue getQueue(VkQueueFlagBits type, uint32_t queueIndex = 0) const;
};
struct InitResult
{
    bool success = false; ///< Indicates that the initialization went successfully.

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator vma = VK_NULL_HANDLE;

    QueueFamilies queueFamilies;

    std::vector<char const *> enabledInstanceExtensions; ///< A list of enabled instance extensions.
    std::vector<char const *> enabledDeviceExtensions; ///< A list of enabled device extensions.
    std::vector<char const *> enabledLayers; ///< A list of enabled layers.
};

/// @brief Initialize vulkan instance together with other stuff.
InitResult init(InitInfo info);

struct SwapchainCreateInfo
{
    struct AllocateInfo
    {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
    };
    
    AllocateInfo alloc;
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkExtent2D size = {0, 0};
};
struct Swapchain
{
    VkSwapchainKHR swapchain;

    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;

    VkSwapchainCreateInfoKHR createInfo;
    VkSurfaceCapabilitiesKHR capabilities;

    SwapchainCreateInfo::AllocateInfo alloc;
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
};
Swapchain makeSwapchain(SwapchainCreateInfo const &ci);
void resizeSwapchain(Swapchain &swapchain, VkExtent2D size);
void destroy(Swapchain &pipeline);


} // namespace vk