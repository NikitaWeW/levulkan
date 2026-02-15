#include <bits/stdc++.h>

#include "nicecs/ecs.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "assimp/Importer.hpp"
#include "assimp/scene.h"
#include "assimp/postprocess.h"

#include "volk.h"
#include "libraries/vk_enum_string_helper.h"
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_DEBUG_INITIALIZE_ALLOCATIONS 1
#define VMA_VULKAN_VERSION 1003000
#include "vk_mem_alloc.h"
#include "GLFW/glfw3.h"

#include "Logging.hpp"
#include "Model.hpp"
#include "Loaders.hpp"
#include "IO.hpp"
#include "Controller.hpp"

template <typename T>
using SparseSet = ecs::sparse_set<T>;

struct SwapchainSupportDetails 
{
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
    VkSurfaceFormatKHR surfaceFormat;
    VkPresentModeKHR surfacePresentMode;
};
struct QueueFamilies
{
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    std::optional<uint32_t> transfer;

    SparseSet<VkDeviceQueueCreateInfo> deviceCreateInfo;
    SparseSet<uint32_t> uniqueFamilies;
    uint32_t count = 0;

    inline bool isComplete() 
    { 
        return graphics.has_value() && present.has_value(); 
    }
};
struct ImageAllocation
{
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageCreateInfo imageCreateInfo;

    glm::uvec2 size = {};
    uint numComponents = 0;
    uint numMipLevels = 1;
    uint index = 0;
};
struct BufferAllocation
{
    VmaAllocation allocation;
    VkBuffer buffer;
    VkDeviceAddress deviceAddress = 0;
    size_t size;
    void *mapped = nullptr;
};
struct VulkanMesh
{
    ecs::entity eModel = 0;
    size_t meshIndex;
    struct Textures
    {
        ImageAllocation albedo;
        ImageAllocation metallic;
        ImageAllocation roughness;
        ImageAllocation ambient;
        ImageAllocation normal;
        ImageAllocation displacement;
    } textures;
    struct Buffers
    {
        BufferAllocation pos;
        BufferAllocation uv;
        BufferAllocation norm;
        BufferAllocation tan;
        BufferAllocation idx;
    } buffers;
    size_t indexCount;
};
struct TextureData
{
    VkImageView view;
    VkSampler sampler;
    VkImageLayout layout; 
};
struct VulkanState
{
    QueueFamilies queueFamilies;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VmaAllocator vma;
    VkSurfaceKHR surface;
    VkRenderPass renderPass;
    VkPipeline pipeline;
    VkCommandPool commandPool;

    std::vector<VkDescriptorImageInfo> textureDescriptorInfos;
    VkDescriptorPool descriptorPoolTex;
    VkDescriptorSet descriptorSetTex;
    VkDescriptorSetLayout descriptorSetLayoutTex;
    VkPipelineLayout pipelineLayout;

    struct Swapchain 
    {
        VkSwapchainKHR swapchain;
        SwapchainSupportDetails swapchainSupport;
        VkSwapchainCreateInfoKHR createInfo;
        uint32_t imageCount;
        std::vector<VkImage> images;
        std::vector<VkImageView> imageViews;
    } swapchain;

    ImageAllocation depthImage;
};

static ecs::registry sReg;

#define ALLOCATOR_HERE VK_NULL_HANDLE
#define CHK(x) { VkResult _result = x; if(_result != VK_SUCCESS) { LOG_ERROR("{}:{}: Failed to {}: {}.", __FILE__, __LINE__, #x, string_VkResult(_result)); LOG_WARN("Aborting..."); abort(); }}

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
constexpr bool ENABLE_VALIDATION_LAYERS = true;
constexpr std::array<char const *, 0> sInstanceExtensions = {
};
constexpr std::array<char const *, 1> sDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

static bool init()
{
    #if LOG_FILENAME
    spdlog::set_pattern("%@ %^%v%$");
    #else
    spdlog::set_pattern("%^%v%$");
    #endif
    sLogger = spdlog::stdout_color_mt("sLogger");
    sLogger->set_level(spdlog::level::trace);

    if(!glfwInit())
    {
        LOG_ERROR("Failed to init glfw!");
        return false;
    }
    if(!glfwVulkanSupported())
    {
        LOG_ERROR("Vulkan is not supported!");
        return false;
    }

    auto res = volkInitialize();
    if(res != VK_SUCCESS)
    {
        LOG_ERROR("Failed to init volk: {}!", string_VkResult(res));
        return false;
    }

    return true;
}


static void insertImageMemoryBarrier(
    VkCommandBuffer         command_buffer,
    VkImage                 image,
    VkAccessFlags           src_access_mask,
    VkAccessFlags           dst_access_mask,
    VkImageLayout           old_layout,
    VkImageLayout           new_layout,
    VkPipelineStageFlags    src_stage_mask,
    VkPipelineStageFlags    dst_stage_mask,
    VkImageSubresourceRange subresource_range)
{
    VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask = dst_stage_mask,
        .dstAccessMask = dst_access_mask,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = subresource_range
    };
    VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    vkCmdPipelineBarrier2(command_buffer, &dependency);
}
static VkDeviceQueueCreateInfo makeDeviceQueueCreateInfo(uint32_t index)
{
    float priorities = 1.0f;
    return VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = index,
        .queueCount = 1,
        .pQueuePriorities = &priorities
    };
}
static void addToFamilies(QueueFamilies &indices, uint32_t i)
{
    indices.deviceCreateInfo[i] = makeDeviceQueueCreateInfo(i);
    indices.uniqueFamilies[i] = i;
    ++indices.count;
}
static QueueFamilies findQueueFamilies(VkPhysicalDevice const &device, VulkanState const &state)
{
    QueueFamilies indices;
    
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for(uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        auto const &family = queueFamilies[i];
        if(family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            indices.graphics = i;
            addToFamilies(indices, i);
        }
        if(family.queueFlags & VK_QUEUE_TRANSFER_BIT)
        {
            indices.transfer = i;
            addToFamilies(indices, i);
        }

        VkBool32 presentSupport;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, state.surface, &presentSupport);
        if(presentSupport)
        {
            indices.present = i;
            addToFamilies(indices, i);
        }

        if(indices.isComplete()) break;
    }

    return indices;
}

static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    for(auto e : reg.view<EventListener>())
        reg.get<EventListener>(e).keyEvents.emplace(window, key, scancode, action, mods);
}
static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos)
{
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    auto cursorPos = glm::dvec2{xpos, ypos};
    for(auto e : reg.view<EventListener>())
    {
        glm::dvec2 delta{0};
        auto &listener = reg.get<EventListener>(e);
        if(listener.prevCursorPos != glm::dvec2{-1})
            delta = cursorPos - listener.prevCursorPos;
        listener.prevCursorPos = cursorPos;
        listener.cursorPosEvents.emplace(window, cursorPos, delta);
    }
}
static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    ecs::registry &reg = *static_cast<ecs::registry *>(glfwGetWindowUserPointer(window));
    for(auto e : reg.view<EventListener>())
        reg.get<EventListener>(e).scrollEvents.emplace(window, glm::dvec2{xoffset, yoffset});
}


static std::vector<char const *> getRequiredExtensions()
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    auto extensions = std::vector<char const *>(glfwExtensions, glfwExtensions + glfwExtensionCount);

    extensions.insert(extensions.end(), sInstanceExtensions.begin(), sInstanceExtensions.end());

    if constexpr(ENABLE_VALIDATION_LAYERS)
        extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    return extensions;
}
static std::vector<char const *> getRequiredDeviceExtensions()
{
    return std::vector<char const *>(sDeviceExtensions.begin(), sDeviceExtensions.end());
}
static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    std::string severity = string_VkDebugUtilsMessageSeverityFlagsEXT(messageSeverity);
    while(severity.find("VK_DEBUG_UTILS_MESSAGE_SEVERITY_") != std::string::npos)
        severity.erase(severity.find("VK_DEBUG_UTILS_MESSAGE_SEVERITY_"), std::string_view("VK_DEBUG_UTILS_MESSAGE_SEVERITY_").size());
    while(severity.find("_BIT_EXT") != std::string::npos)
        severity.erase(severity.find("_BIT_EXT"), std::string_view("_BIT_EXT").size());
    for(auto& character : severity) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

    std::string type = string_VkDebugUtilsMessageTypeFlagsEXT(messageType);
    while(type.find("VK_DEBUG_UTILS_MESSAGE_TYPE_") != std::string::npos)
        type.erase(type.find("VK_DEBUG_UTILS_MESSAGE_TYPE_"), std::string_view("VK_DEBUG_UTILS_MESSAGE_TYPE_").size());
    while(type.find("_BIT_EXT") != std::string::npos)
        type.erase(type.find("_BIT_EXT"), std::string_view("_BIT_EXT").size());
    for(auto& character : type) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));

    spdlog::level::level_enum level;
    switch(messageSeverity) 
    {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        level = spdlog::level::err;
        break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        level = spdlog::level::warn;
        break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        level = spdlog::level::info;
        break;
        default:
        level = spdlog::level::debug;
        break;
    }

    LOG(level, "vulkan {} {} message:\n{}", type, severity, pCallbackData->pMessage);

    return VK_FALSE;
}

static std::pair<VkInstance, bool> createInstance()
{
    VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "levulkan",
        .apiVersion = VK_API_VERSION_1_3
    };

    uint numExtensionsAvailable;
    vkEnumerateInstanceExtensionProperties(nullptr, &numExtensionsAvailable, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(numExtensionsAvailable);
    vkEnumerateInstanceExtensionProperties(nullptr, &numExtensionsAvailable, availableExtensions.data());

    auto extensions = getRequiredExtensions();

    bool notFound = false;
    std::vector<char const *> enabledExtensions;
    for(auto const &extension : extensions)
    {
        if(std::find_if(availableExtensions.begin(), availableExtensions.end(), [&](VkExtensionProperties const &prop){ return std::strcmp(prop.extensionName, extension) == 0;}) == availableExtensions.end())
        {
            LOG_ERROR("Required extension \"{}\" not present!", extension);
            notFound = true;
        } else {
            enabledExtensions.emplace_back(extension);
        }
    }


    LOG_INFO("Instance extensions: {}", extensions);
    if(notFound)
        LOG_WARN("Enabled extensions: {}", enabledExtensions);

    std::vector<char const *> layers;
    if constexpr(ENABLE_VALIDATION_LAYERS)
    {
        std::vector<char const *> const requestedLayers = {
            "VK_LAYER_KHRONOS_validation"
        };

        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for(auto const &layer : requestedLayers)
        {
            auto pos = std::find_if(availableLayers.begin(), availableLayers.end(), [&](VkLayerProperties const &p){ return std::strcmp(p.layerName, layer) == 0; });
            if(pos == availableLayers.end())
            {
                LOG_ERROR("Layer not available: \"{}\"", layer);
            } else {
                layers.emplace_back(layer);
            }
        }
    } else {
        LOG_WARN("Validation layers disabled!");
    }
    LOG_INFO("Layers: {}", layers);

    VkInstanceCreateInfo instanceCI{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size()),
        .ppEnabledExtensionNames = enabledExtensions.data(),
    };

    VkInstance instance;
    auto res = vkCreateInstance(&instanceCI, ALLOCATOR_HERE, &instance);
    if(res != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create instance: {}!", string_VkResult(res));
        return {{}, false};
    }

    volkLoadInstance(instance);
    
    return std::make_pair(instance, true);
}
bool checkDeviceExtensionSupport(VkPhysicalDevice device, std::vector<char const *> deviceExtensions) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}
static VkSurfaceFormatKHR chooseSwapSurfaceFormat(SwapchainSupportDetails const &details)
{
    for(auto const &format : details.formats)
    {
        if(format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return format;
    }

    LOG_ERROR("Failed to find swap surface format!");
    return details.formats.size() > 0 ? details.formats[0] : VkSurfaceFormatKHR{};
}
static VkPresentModeKHR chooseSwapPresentMode(SwapchainSupportDetails const &details)
{
    for (const auto& availablePresentMode : details.presentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            return availablePresentMode;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}
static SwapchainSupportDetails getSwapchainSupport(VkPhysicalDevice const &dev, VulkanState const &state) {
    SwapchainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, state.surface, &details.capabilities);
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, state.surface, &formatCount, nullptr);

    if(formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, state.surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, state.surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, state.surface, &presentModeCount, details.presentModes.data());
    }

    details.surfaceFormat = chooseSwapSurfaceFormat(details);
    details.surfacePresentMode = chooseSwapPresentMode(details);

    return details;
}
static bool isDeviceSuitable(VkPhysicalDevice dev, VulkanState const &state)
{
    VkPhysicalDeviceFeatures2 features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    vkGetPhysicalDeviceFeatures2(dev, &features);
    auto swapchainSupport = getSwapchainSupport(dev, state);
    return 
        features.features.geometryShader && 
        findQueueFamilies(dev, state).isComplete() && 
        checkDeviceExtensionSupport(dev, getRequiredDeviceExtensions()) &&
        swapchainSupport.formats.size() > 0 && swapchainSupport.presentModes.size() > 0;
}
static bool pickPhysicalDevice(VulkanState &state)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(state.instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(state.instance, &deviceCount, devices.data());

    if(devices.size() == 0)
        LOG_ERROR("No vulkan devices!");

    bool deviceFound = false;
    for(auto const &dev : devices)
    {
        // VkPhysicalDeviceProperties2 deviceProperties{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        // VkPhysicalDeviceFeatures2 deviceFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        // vkGetPhysicalDeviceProperties2(dev, &deviceProperties);
        // vkGetPhysicalDeviceFeatures2(dev, &deviceFeatures);

        if(!isDeviceSuitable(dev, state))
            continue;

        state.physicalDevice = dev;
        deviceFound = true;
    }    

    return deviceFound;
}
static VkDevice createDevice(VkPhysicalDevice const &physicalDevice, QueueFamilies const &families)
{
    VkPhysicalDeviceVulkan12Features enabledVk12Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .descriptorIndexing = true,
        .descriptorBindingVariableDescriptorCount = true,
        .runtimeDescriptorArray = true,
        .bufferDeviceAddress = true
    };
    VkPhysicalDeviceVulkan13Features enabledVk13Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &enabledVk12Features,
        .synchronization2 = true,
        .dynamicRendering = true,
    };
    VkPhysicalDeviceFeatures2 deviceFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &enabledVk13Features
    };
    vkGetPhysicalDeviceFeatures2(physicalDevice, &deviceFeatures);
    auto extensions = getRequiredDeviceExtensions();
    VkDeviceCreateInfo deviceCI{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabledVk13Features,
        .queueCreateInfoCount = static_cast<uint32_t>(families.deviceCreateInfo.dense().size()),
        .pQueueCreateInfos = families.deviceCreateInfo.dense().data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
        .pEnabledFeatures = &deviceFeatures.features,
    };
    VkDevice device;
    vkCreateDevice(physicalDevice, &deviceCI, ALLOCATOR_HERE, &device);
    volkLoadDevice(device);

    return device;
}
static void createAllocator(VulkanState &state)
{
    VmaAllocatorCreateInfo allocatorCI{
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, 
        .physicalDevice = state.physicalDevice,
        .device = state.device,
        .instance = state.instance,
        .vulkanApiVersion = VK_API_VERSION_1_3
    };
    VmaVulkanFunctions functions;
    CHK(vmaImportVulkanFunctionsFromVolk(&allocatorCI, &functions));
    allocatorCI.pVulkanFunctions = &functions;
    CHK(vmaCreateAllocator(&allocatorCI, &state.vma));
}
static VkQueue getQueue(VkDevice dev, uint32_t index)
{
    VkQueue queue;
    vkGetDeviceQueue(dev, index, 0, &queue);
    return queue;
}
static VkExtent2D chooseExtent(SwapchainSupportDetails const &details, Window const &window)
{
    if (details.capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return details.capabilities.currentExtent;
    } else {
        VkExtent2D actualExtent = {
            static_cast<uint32_t>(window.size.x),
            static_cast<uint32_t>(window.size.y)
        };

        actualExtent.width = glm::clamp(actualExtent.width, details.capabilities.minImageExtent.width, details.capabilities.maxImageExtent.width);
        actualExtent.height = glm::clamp(actualExtent.height, details.capabilities.minImageExtent.height, details.capabilities.maxImageExtent.height);

        return actualExtent;
    }
}
static void getSwapchainImages(VulkanState &state)
{
    for(auto &view : state.swapchain.imageViews)
        vkDestroyImageView(state.device, view, ALLOCATOR_HERE);

    vkGetSwapchainImagesKHR(state.device, state.swapchain.swapchain, &state.swapchain.imageCount, nullptr);
    state.swapchain.images.resize(state.swapchain.imageCount);
    vkGetSwapchainImagesKHR(state.device, state.swapchain.swapchain, &state.swapchain.imageCount, state.swapchain.images.data());
    state.swapchain.imageViews.resize(state.swapchain.imageCount);
    
    for(size_t i = 0; i < state.swapchain.imageCount; i++) {
        VkImageViewCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = state.swapchain.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = state.swapchain.swapchainSupport.surfaceFormat.format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        CHK(vkCreateImageView(state.device, &createInfo, ALLOCATOR_HERE, &state.swapchain.imageViews[i]));
    }
}
static bool createSwapchain(VulkanState &state, Window const &window)
{
    SwapchainSupportDetails swapchainSupport = getSwapchainSupport(state.physicalDevice, state);
    VkExtent2D extent = chooseExtent(swapchainSupport, window);

    state.swapchain.createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = state.surface,
        .minImageCount = swapchainSupport.capabilities.minImageCount + 1,
        .imageFormat = swapchainSupport.surfaceFormat.format,
        .imageColorSpace = swapchainSupport.surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = swapchainSupport.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = swapchainSupport.surfacePresentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE
    };

    if(state.queueFamilies.uniqueFamilies.size() == state.queueFamilies.count)
    {
        LOG_TRACE("VK_SHARING_MODE_CONCURRENT");
        state.swapchain.createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        state.swapchain.createInfo.queueFamilyIndexCount = state.queueFamilies.uniqueFamilies.size();
        state.swapchain.createInfo.pQueueFamilyIndices = state.queueFamilies.uniqueFamilies.dense().data();
    } else {
        LOG_TRACE("VK_SHARING_MODE_EXCLUSIVE");
        state.swapchain.createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        state.swapchain.createInfo.queueFamilyIndexCount = 0;
        state.swapchain.createInfo.pQueueFamilyIndices = nullptr;
    }

    auto res = vkCreateSwapchainKHR(state.device, &state.swapchain.createInfo, ALLOCATOR_HERE, &state.swapchain.swapchain);
    if(res != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create swap chain: {}", string_VkResult(res));
        return false;
    }

    state.swapchain.swapchainSupport = getSwapchainSupport(state.physicalDevice, state);
    getSwapchainImages(state);

    return true;
}
static std::vector<char> readFileBinary(std::string_view filename) 
{
    std::ifstream file(std::string{filename}, std::ios::ate | std::ios::binary);

    if(!file.is_open()) 
    {
        LOG_ERROR("Failed to open file \"{}\"", filename);
        return {};
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    return buffer;
}
static VkShaderModule createShaderModule(VkDevice const &device, std::vector<char> const &code) 
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<uint32_t const *>(code.data());
    
    VkShaderModule module;
    auto res = vkCreateShaderModule(device, &createInfo, ALLOCATOR_HERE, &module);

    if(res != VK_SUCCESS)
        LOG_ERROR("Failed to create a shader module: {}", string_VkResult(res));

    return module;
}
void createCommandPool(VulkanState &state)
{
    VkCommandPoolCreateInfo poolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = state.queueFamilies.graphics.value(),
    };
    CHK(vkCreateCommandPool(state.device, &poolCreateInfo, ALLOCATOR_HERE, &state.commandPool));
}

static std::string printTexture(ecs::entity e, ecs::registry const &reg)
{
    if(!reg.valid(e))
        return fmt::format("e{} -- INVALID", e);
    auto const &texture = reg.get<Texture>(e);
    return fmt::format("e{}, \"{:<30} {}x{}, {:>3}", e, texture.path + "\",", texture.bitmap.size.x, texture.bitmap.size.y, (texture.srgb ? "srgb" : "not srgb"));
}
static void printModelData(ecs::entity e, ecs::registry const &reg)
{
    assert(reg.has<Model>(e));
    Model const &model = reg.get<Model>(e);
    LOG_INFO("");
    LOG_INFO("Model: e{}: \"{}\"", e, model.path);
    LOG_INFO("Skeleton: ");
    LOG_INFO("  Bone map size / number of bones: {}", model.skeleton.boneMap.size());
    if(model.skeleton.boneMap.size() <= 30)
        for(auto const &[name, id] : model.skeleton.boneMap)
            LOG_INFO("    [\"{}\": {}]", name, id);

    LOG_INFO("Animations: {}", model.animations.size());
    for(auto const &animation : model.animations)
    {
        LOG_INFO("-----------------");
        LOG_INFO("Animation: \"{}\"", animation.name);
        LOG_INFO("  Duration: {} ticks, tps: {}", animation.durationTicks, animation.ticksPerSecond);
        LOG_INFO("  Bones size: {}", animation.bones.size());
    }

    LOG_INFO("Meshes: {}", model.meshes.size());
    for(auto const &mesh : model.meshes)
    {
        LOG_INFO("-----------------");

        LOG_INFO("Geometry:");
        LOG_INFO("  Triangles: {}", mesh.geometry.indices.size() / 3);
        LOG_INFO("  Indices:   {}", mesh.geometry.indices.size());
        LOG_INFO("  Positions: {}", mesh.geometry.positions.size());
        LOG_INFO("  TexCoords: {}", mesh.geometry.texCoords.size());
        LOG_INFO("  Normals:   {}", mesh.geometry.normals.size());
        LOG_INFO("  Tangents:  {}", mesh.geometry.tangents.size());
        LOG_INFO("  BoneIDs:   {}", mesh.geometry.boneIDs.size());
        LOG_INFO("  Weights:   {}", mesh.geometry.weights.size());
        
        LOG_INFO("Material:");
        LOG_INFO("Textures:");
        LOG_INFO("  Albedo:       {}", printTexture(mesh.material.textures.albedo, reg));
        LOG_INFO("  Metallic:     {}", printTexture(mesh.material.textures.metallic, reg));
        LOG_INFO("  Roughness:    {}", printTexture(mesh.material.textures.roughness, reg));
        LOG_INFO("  Ambient:      {}", printTexture(mesh.material.textures.ambient, reg));
        LOG_INFO("  Normal:       {}", printTexture(mesh.material.textures.normal, reg));
        LOG_INFO("  Displacement: {}", printTexture(mesh.material.textures.displacement, reg));
        LOG_INFO("Properties:");
        LOG_INFO("  Ambient:       {}", fmt::streamed(mesh.material.properties.ambient));
        LOG_INFO("  Albedo:        {}", fmt::streamed(mesh.material.properties.albedo));
        LOG_INFO("  Specular:      {}", fmt::streamed(mesh.material.properties.specular));
        LOG_INFO("  Emission:      {}", fmt::streamed(mesh.material.properties.emission));
        LOG_INFO("  Shininess:     {}", mesh.material.properties.shininess);
        LOG_INFO("  Metallic:      {}", mesh.material.properties.metallic);
        LOG_INFO("  IOR:           {}", mesh.material.properties.ior);
    }
}
template<typename T>
BufferAllocation allocateBuffer(VulkanState &state, std::vector<T> const &data)
{
    BufferAllocation buffer;
    buffer.size = data.size() * sizeof(T);

    VkBufferCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer.size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
    };
    VmaAllocationCreateInfo allocCI{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };

    CHK(vmaCreateBuffer(state.vma, &ci, &allocCI, &buffer.buffer, &buffer.allocation, nullptr));

    void *bufferPtr = nullptr;
    CHK(vmaMapMemory(state.vma, buffer.allocation, &bufferPtr));
    std::memcpy(bufferPtr, data.data(), buffer.size);
    vmaUnmapMemory(state.vma, buffer.allocation);

/* 
    VkBufferDeviceAddressInfo bdaInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = buffer.buffer
    };
    buffer.deviceAddress = vkGetBufferDeviceAddress(state.device, &bdaInfo);
 */

    return buffer;
}
static ImageAllocation allocateTexture(VulkanState &state, ecs::entity eTexture)
{
    if(!sReg.valid(eTexture))
    {
        LOG_ERROR("Invalid texture entity: {}!", eTexture);
        assert(false);
        return {};
    }
    Texture const &texture = sReg.get<Texture>(eTexture);
    assert(texture.bitmap.numComponents == 3);
    ImageAllocation image;
    image.format = texture.srgb ? VK_FORMAT_R8G8B8_SRGB : VK_FORMAT_R8G8B8_UNORM;
    image.numMipLevels = texture.numMipLevels;
    image.numComponents = texture.bitmap.numComponents;
    image.size = texture.bitmap.size;

    VkImageCreateInfo imageCI{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = image.format,
        .extent = {.width = image.size.x, .height = image.size.y, .depth = 1 },
        .mipLevels = image.numMipLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    VmaAllocationCreateInfo allocCI{
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    CHK(vmaCreateImage(state.vma, &imageCI, &allocCI, &image.image, &image.allocation, nullptr));

    VkBuffer imgSrcBuffer{};
    VmaAllocation imgSrcAllocation{};
    VkBufferCreateInfo imgSrcBufferCI{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(texture.bitmap.pixels[0]) * texture.bitmap.pixels.size(),
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    };
    VmaAllocationCreateInfo imgSrcAllocCI{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    CHK(vmaCreateBuffer(state.vma, &imgSrcBufferCI, &imgSrcAllocCI, &imgSrcBuffer, &imgSrcAllocation, nullptr));

    void* imgSrcBufferPtr = nullptr;
    CHK(vmaMapMemory(state.vma, imgSrcAllocation, &imgSrcBufferPtr));
    std::memcpy(imgSrcBufferPtr, texture.bitmap.pixels.data(), sizeof(texture.bitmap.pixels[0]) * texture.bitmap.pixels.size());
    vmaUnmapMemory(state.vma, imgSrcAllocation);

    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo commandBufferAllocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = state.commandPool,
        .commandBufferCount = 1,
    };
    CHK(vkAllocateCommandBuffers(state.device, &commandBufferAllocInfo, &commandBuffer));
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    CHK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
    VkBufferImageCopy bufferCopyRegion = {
        .bufferOffset = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageExtent = {
            .width = image.size.x,
            .height = image.size.y,
            .depth = 1,
        }
    };
    vkCmdCopyBufferToImage(commandBuffer, imgSrcBuffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

/* FIXME
    // FIXME: validation layers screaming
    for(uint32_t i = 1; i < image.numMipLevels; i++)
    {
        VkImageBlit2 imageBlit{
            .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel   = i - 1,
                .layerCount = 1,
            },
            .srcOffsets = {
                { 0, 0, 0 },
                { int32_t(image.size.x >> (i - 1)), int32_t(image.size.y >> (i - 1)), 1 }
            },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel   = i,
                .layerCount = 1,
            },
            .dstOffsets = {
                { 0, 0, 0 },
                { int32_t(image.size.x >> i), int32_t(image.size.y >> i), 1 }
            }
        };
        VkBlitImageInfo2 imageBlitInfo{
            .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
            .srcImage = image.image,
            .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .dstImage = image.image,
            .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .regionCount = 1,
            .pRegions = &imageBlit,
            .filter = VK_FILTER_LINEAR
        };

        // insertImageMemoryBarrier(commandBuffer, image.image,
        //     0,
        //     VK_ACCESS_TRANSFER_WRITE_BIT,
        //     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        //     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        //     VK_PIPELINE_STAGE_TRANSFER_BIT,
        //     VK_PIPELINE_STAGE_TRANSFER_BIT,
        //     {VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1}
        // );

        vkCmdBlitImage2(commandBuffer, &imageBlitInfo);

        // insertImageMemoryBarrier(commandBuffer, image.image, 
        //     VK_ACCESS_TRANSFER_WRITE_BIT,
        //     VK_ACCESS_TRANSFER_READ_BIT,
        //     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        //     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        //     VK_PIPELINE_STAGE_TRANSFER_BIT,
        //     VK_PIPELINE_STAGE_TRANSFER_BIT,
        //     {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        // );

        // insertImageMemoryBarrier(commandBuffer, image.image, 
        //     VK_ACCESS_TRANSFER_WRITE_BIT,
        //     VK_ACCESS_TRANSFER_READ_BIT,
        //     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        //     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        //     VK_PIPELINE_STAGE_TRANSFER_BIT,
        //     VK_PIPELINE_STAGE_TRANSFER_BIT,
        //     {VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1}
        // );
    }
    insertImageMemoryBarrier(commandBuffer, image.image,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, image.numMipLevels, 0, 1}
    );

    vkEndCommandBuffer(commandBuffer);

    VkFence fence;
    VkFenceCreateInfo fenceCI{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    vkCreateFence(state.device, &fenceCI, ALLOCATOR_HERE, &fence);

    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
    };
    static auto queue = getQueue(state.device, state.queueFamilies.transfer);
    vkQueueSubmit(queue, 1, &submitInfo, fence);
    vkWaitForFences(state.device, 1, &fence, true, 0);
*/
    VkImageViewCreateInfo texVewCI{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image.format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = image.numMipLevels, .layerCount = 1 }
    };
    CHK(vkCreateImageView(state.device, &texVewCI, nullptr, &image.view));

    VkSamplerCreateInfo samplerCI{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 8.0f, // 8 is a widely supported value for max anisotropy
        .maxLod = (float) image.numMipLevels,
    };
    CHK(vkCreateSampler(state.device, &samplerCI, nullptr, &image.sampler));

    image.index = state.textureDescriptorInfos.size();
    state.textureDescriptorInfos.emplace_back(VkDescriptorImageInfo{
        .sampler = image.sampler,
        .imageView = image.view,
        .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL
    });

    vmaDestroyBuffer(state.vma, imgSrcBuffer, imgSrcAllocation);

    return image;
}
static void makeDescriptors(VulkanState &state)
{
    VkDescriptorBindingFlags descVariableFlag = VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo descBindingFlags{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = 1,
        .pBindingFlags = &descVariableFlag
    };
    VkDescriptorSetLayoutBinding descLayoutBindingTex{
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = static_cast<uint32_t>(state.textureDescriptorInfos.size()),
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };
    VkDescriptorSetLayoutCreateInfo descLayoutTexCI{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &descBindingFlags,
        .bindingCount = 1,
        .pBindings = &descLayoutBindingTex
    };
    CHK(vkCreateDescriptorSetLayout(state.device, &descLayoutTexCI, nullptr, &state.descriptorSetLayoutTex));

    VkDescriptorPoolSize poolSize{
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = static_cast<uint32_t>(state.textureDescriptorInfos.size())
    };
    VkDescriptorPoolCreateInfo descPoolCI{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    CHK(vkCreateDescriptorPool(state.device, &descPoolCI, nullptr, &state.descriptorPoolTex));

    uint32_t variableDescCount = static_cast<uint32_t>(state.textureDescriptorInfos.size());
    VkDescriptorSetVariableDescriptorCountAllocateInfo variableDescCountAI{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT,
        .descriptorSetCount = 1,
        .pDescriptorCounts = &variableDescCount
    };
    VkDescriptorSetAllocateInfo texDescSetAlloc{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = &variableDescCountAI,
        .descriptorPool = state.descriptorPoolTex,
        .descriptorSetCount = 1,
        .pSetLayouts = &state.descriptorSetLayoutTex
    };
    CHK(vkAllocateDescriptorSets(state.device, &texDescSetAlloc, &state.descriptorSetTex));

    VkWriteDescriptorSet writeDescSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = state.descriptorSetTex,
        .dstBinding = 0,
        .descriptorCount = static_cast<uint32_t>(state.textureDescriptorInfos.size()),
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
        .pImageInfo = state.textureDescriptorInfos.data()
    };
    vkUpdateDescriptorSets(state.device, 1, &writeDescSet, 0, nullptr);
}
static std::optional<VulkanMesh> loadModel(VulkanState &state, std::string_view path, std::optional<Material> material = {})
{
    static ModelLoader loader(sReg);
    
    auto eModel = loader.loadFromFile(path);

    if(!eModel)
    {
        return std::nullopt;
    } else {
        printModelData(eModel, sReg);
    }

    auto &model = sReg.get<Model>(eModel);
/* 
    if(material.has_value()) {
        Material mat = material.value();
        auto defaultMaterial = loader.getDefaultMaterial();
        if(mat.textures.albedo       == INVALID_ENTITY) mat.textures.albedo       = defaultMaterial.textures.albedo;
        if(mat.textures.metallic     == INVALID_ENTITY) mat.textures.metallic     = defaultMaterial.textures.metallic;
        if(mat.textures.roughness    == INVALID_ENTITY) mat.textures.roughness    = defaultMaterial.textures.roughness;
        if(mat.textures.normal       == INVALID_ENTITY) mat.textures.normal       = defaultMaterial.textures.normal;
        if(mat.textures.displacement == INVALID_ENTITY) mat.textures.displacement = defaultMaterial.textures.displacement;

        for(auto &mesh : model.meshes)
            mesh.material = mat;
    }
 */

    if(model.meshes.size() > 1)
        LOG_WARN("Multi-mesh models are not yet supported! \"{}\"", path);
    if(model.meshes.size() == 0)
    {
        LOG_ERROR("Model \"{}\" has no meshes!", path);
        return std::nullopt;
    }

    auto const &mesh = model.meshes.at(0);
    return VulkanMesh{
        .eModel = eModel,
        .meshIndex = 0,
        .textures = {
            .albedo       = allocateTexture(state, mesh.material.textures.albedo),
            .metallic     = allocateTexture(state, mesh.material.textures.metallic),
            .roughness    = allocateTexture(state, mesh.material.textures.roughness),
            .ambient      = allocateTexture(state, mesh.material.textures.ambient),
            .normal       = allocateTexture(state, mesh.material.textures.normal),
            .displacement = allocateTexture(state, mesh.material.textures.displacement),
        },
        .buffers = {
            .pos  = allocateBuffer(state, mesh.geometry.positions),
            .uv   = allocateBuffer(state, mesh.geometry.texCoords),
            .norm = allocateBuffer(state, mesh.geometry.normals),
            .tan  = allocateBuffer(state, mesh.geometry.tangents),
            .idx  = allocateBuffer(state, mesh.geometry.indices),
        },
        .indexCount = mesh.geometry.indices.size()
    };
}
template<typename T>
static T valueOrAbort(std::optional<T> const &o)
{
    if(!o.has_value())
        abort();

    return o.value();
}
static void makeDepthAttachment(VulkanState &state, VkExtent2D extent)
{
    std::vector<VkFormat> depthFormatList{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
    for(VkFormat& format : depthFormatList) {
        VkFormatProperties2 formatProperties{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
        vkGetPhysicalDeviceFormatProperties2(state.physicalDevice, format, &formatProperties);
        if(formatProperties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            state.depthImage.format = format;
            break;
        }
    }
    state.depthImage.size = {extent.width, extent.height};
    state.depthImage.numComponents = 1;

    state.depthImage.imageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = state.depthImage.format,
        .extent{.width = extent.width, .height = extent.height, .depth = 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo allocCI{
        .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO
    };
    CHK(vmaCreateImage(state.vma, &state.depthImage.imageCreateInfo, &allocCI, &state.depthImage.image, &state.depthImage.allocation, nullptr));

    VkImageViewCreateInfo depthViewCI{ 
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = state.depthImage.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = state.depthImage.format,
        .subresourceRange{ .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
    };
    CHK(vkCreateImageView(state.device, &depthViewCI, ALLOCATOR_HERE, &state.depthImage.view));
}

static void makePipeline(VulkanState &state, VkShaderModule shaderModule, VkExtent2D extent)
{
    VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .size = sizeof(VkDeviceAddress)
    };
    VkPipelineLayoutCreateInfo pipelineLayoutCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &state.descriptorSetLayoutTex,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange
    };
    CHK(vkCreatePipelineLayout(state.device, &pipelineLayoutCI, nullptr, &state.pipelineLayout));

    // Bindings
    const std::array<VkVertexInputBindingDescription, 4> vertexInputBindings = {
        VkVertexInputBindingDescription{ 0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX },
        VkVertexInputBindingDescription{ 1, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX },
        VkVertexInputBindingDescription{ 2, sizeof(glm::vec2), VK_VERTEX_INPUT_RATE_VERTEX },
        VkVertexInputBindingDescription{ 3, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX },
    };

    // Attributes
    const std::array<VkVertexInputAttributeDescription, 4> vertexInputAttributes = {
        VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
        VkVertexInputAttributeDescription{ 1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0 },
        VkVertexInputAttributeDescription{ 2, 2, VK_FORMAT_R32G32_SFLOAT, 0 },
        VkVertexInputAttributeDescription{ 3, 3, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
    };

    VkPipelineVertexInputStateCreateInfo vertexInputState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size()),
        .pVertexBindingDescriptions = vertexInputBindings.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size()),
        .pVertexAttributeDescriptions = vertexInputAttributes.data(),
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages{
        { 
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = shaderModule, .pName = "main"
        },
        { 
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = shaderModule, .pName = "main" 
        }
    };

    VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };
    std::array<VkDynamicState, 2> dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = dynamicStates.size(),
        .pDynamicStates = dynamicStates.data()
    };

    makeDepthAttachment(state, extent);
    VkPipelineDepthStencilStateCreateInfo depthStencilState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {}
    };

    VkPipelineRenderingCreateInfo renderingCI{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &state.swapchain.swapchainSupport.surfaceFormat.format,
        .depthAttachmentFormat = state.depthImage.format
    };

    // HACK: blending
    VkPipelineColorBlendAttachmentState blendAttachment{
        .colorWriteMask = 0xF
    };
    VkPipelineColorBlendStateCreateInfo colorBlendState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blendAttachment
    };
    VkPipelineRasterizationStateCreateInfo rasterizationState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .lineWidth = 1.0f
    };
    VkPipelineMultisampleStateCreateInfo multisampleState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };

    VkGraphicsPipelineCreateInfo pipelineCI{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingCI,
        .stageCount = 2,
        .pStages = shaderStages.data(),
        .pVertexInputState = &vertexInputState,
        .pInputAssemblyState = &inputAssemblyState,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizationState,
        .pMultisampleState = &multisampleState,
        .pDepthStencilState = &depthStencilState,
        .pColorBlendState = &colorBlendState,
        .pDynamicState = &dynamicState,
        .layout = state.pipelineLayout
    };
    CHK(vkCreateGraphicsPipelines(state.device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &state.pipeline));
}
static void resizeSwapchain(VulkanState &state, VkExtent2D extent)
{
    CHK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(state.physicalDevice, state.surface, &state.swapchain.swapchainSupport.capabilities));

    state.swapchain.createInfo.oldSwapchain = state.swapchain.swapchain;
    state.swapchain.createInfo.imageExtent = extent;

    CHK(vkCreateSwapchainKHR(state.device, &state.swapchain.createInfo, ALLOCATOR_HERE, &state.swapchain.swapchain));
    // Image count and image views
    getSwapchainImages(state);
    vkDestroySwapchainKHR(state.device, state.swapchain.createInfo.oldSwapchain, ALLOCATOR_HERE);
}

int main(int argc, char const **argv)
{
    // Non-vulkan setup
    if(!init())
    {
        LOG_ERROR("Failed to init!");
        return -1;
    }

    VulkanState &state = sReg.get<VulkanState>(sReg.create<VulkanState>());
    Window &mainWindow = sReg.get<Window>(sReg.create<Window>());

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    mainWindow.handle = glfwCreateWindow(800, 600, "levulkan", nullptr, nullptr);
    glfwGetWindowSize(mainWindow.handle, reinterpret_cast<int *>(&mainWindow.size.x), reinterpret_cast<int *>(&mainWindow.size.y));
    if(glfwRawMouseMotionSupported())
        glfwSetInputMode(mainWindow.handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwSetWindowUserPointer(mainWindow.handle, &sReg);
    glfwSetKeyCallback(mainWindow.handle, keyCallback);
    glfwSetCursorPosCallback(mainWindow.handle, cursorPosCallback);
    glfwSetScrollCallback(mainWindow.handle, scrollCallback);

    // Actual vulkan setup

    {
        auto [instance, instanceCreated] = createInstance();
        if(!instanceCreated)
            return -1;
        state.instance = instance;
    }

    {
        auto res = glfwCreateWindowSurface(state.instance, mainWindow.handle, ALLOCATOR_HERE, &state.surface);
        if(res != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create window surface: {}!", string_VkResult(res));
            return -1;
        }
    }

    VkDebugUtilsMessengerEXT debugMessenger;
    if constexpr(ENABLE_VALIDATION_LAYERS)
    {
        VkDebugUtilsMessengerCreateInfoEXT debugMessengerCI{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            // .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = vulkanDebugCallback,
            .pUserData = nullptr
        };
        vkCreateDebugUtilsMessengerEXT(state.instance, &debugMessengerCI, ALLOCATOR_HERE, &debugMessenger);
    }

    bool physicalDeviceFound = pickPhysicalDevice(state);
    LOG_INFO("Device extensions: {}", getRequiredDeviceExtensions());
    if(!physicalDeviceFound)
    {
        LOG_ERROR("No suitable physical device found!");
        return -1;
    } else {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(state.physicalDevice, &properties);
        LOG_INFO("Physical device: {}", properties.deviceName);
    }

    state.queueFamilies = findQueueFamilies(state.physicalDevice, state);
    state.device = createDevice(state.physicalDevice, state.queueFamilies);

    createAllocator(state);

    assert(state.queueFamilies.isComplete());

    if(!createSwapchain(state, mainWindow))
        return -1;
    VkExtent2D extent = chooseExtent(state.swapchain.swapchainSupport, mainWindow);

    LOG_INFO("{} swapchain images", state.swapchain.images.size());
    assert(state.swapchain.images.size() == state.swapchain.images.size());

    createCommandPool(state);

    auto eMesh = sReg.create(valueOrAbort(loadModel(state, "assets/suzanne.glb", Material{
        .textures = {
            .albedo = TextureLoader{sReg}.loadFromFile("assets/wood.jpg")
        }
    })));
    auto &mesh = sReg.get<VulkanMesh>(eMesh);

    makeDescriptors(state);

    std::array<BufferAllocation, MAX_FRAMES_IN_FLIGHT> shaderDataBuffers;
    std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> commandBuffers;
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> presentSemaphores;
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> fences;
    std::vector<VkSemaphore> renderSemaphores;
    struct ShaderUniformData 
    {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 model[3];
        glm::vec3 lightPos{0.0f, -10.0f, 10.0f};
        uint32_t selected{1};
    } shaderData{};

    // TODO: switch back to glsl
    auto shaderModule = createShaderModule(state.device, readFileBinary("shaders-bin/basic.slang.spv")); 

    makePipeline(state, shaderModule, extent);

    for(uint i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) 
    {
        VkBufferCreateInfo uBufferCI{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(ShaderUniformData),
            .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        };
        VmaAllocationCreateInfo uBufferAllocCI{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO
        };

        auto &shaderDataBuffer = shaderDataBuffers[i];
        CHK(vmaCreateBuffer(state.vma, &uBufferCI, &uBufferAllocCI, &shaderDataBuffer.buffer, &shaderDataBuffer.allocation, nullptr));
        CHK(vmaMapMemory(state.vma, shaderDataBuffer.allocation, &shaderDataBuffer.mapped));

        VkBufferDeviceAddressInfo bdaInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = shaderDataBuffer.buffer
        };
        shaderDataBuffer.deviceAddress = vkGetBufferDeviceAddress(state.device, &bdaInfo);

        VkCommandBufferAllocateInfo commandBufferAllocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = state.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        CHK(vkAllocateCommandBuffers(state.device, &commandBufferAllocateInfo, &commandBuffers[i]));

        VkSemaphoreCreateInfo semaphoreCI{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .flags = 0
        };
        vkCreateSemaphore(state.device, &semaphoreCI, ALLOCATOR_HERE, &presentSemaphores[i]);

        VkFenceCreateInfo fenceCI{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT
        };

        CHK(vkCreateFence(state.device, &fenceCI, ALLOCATOR_HERE, &fences[i]));
    }

    renderSemaphores.resize(state.swapchain.imageCount);
    for(auto &semaphore : renderSemaphores)
    {
        VkSemaphoreCreateInfo semaphoreCI{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };
        vkCreateSemaphore(state.device, &semaphoreCI, ALLOCATOR_HERE, &semaphore);
    }


// === === === === === === === === === === === === === === === ===

    uint frameIndex = 0;
    uint imageIndex = 0;
    float deltatime = 1e-6;

    Controller::Camera &camera = sReg.get<Controller::Camera>(Controller::createCamera(sReg, {0, 2, 4}, {0, 0, 0}));
    Controller cameraController;

    VkQueue graphicsQueue = getQueue(state.device, state.queueFamilies.graphics.value());
    VkQueue presentQueue = getQueue(state.device, state.queueFamilies.present.value());

    bool shouldResize = false;
    while(!glfwWindowShouldClose(mainWindow.handle))
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Poll events
        glfwPollEvents();
        auto prevSize = mainWindow.size;
        glfwGetWindowSize(mainWindow.handle, reinterpret_cast<int *>(&mainWindow.size.x), reinterpret_cast<int *>(&mainWindow.size.y));
        VkExtent2D windowExtent = { .width = mainWindow.size.x, .height = mainWindow.size.y };

        shouldResize = shouldResize || prevSize != mainWindow.size;
        
        // Resize swapchain
        if(shouldResize) {
            vkDeviceWaitIdle(state.device);

            resizeSwapchain(state, windowExtent);

            vmaDestroyImage(state.vma, state.depthImage.image, state.depthImage.allocation);
            vkDestroyImageView(state.device, state.depthImage.view, nullptr);
            state.depthImage.imageCreateInfo.extent = { windowExtent.width, windowExtent.height, 1 };
            VmaAllocationCreateInfo allocCI{
                .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
                .usage = VMA_MEMORY_USAGE_AUTO
            };
            CHK(vmaCreateImage(state.vma, &state.depthImage.imageCreateInfo, &allocCI, &state.depthImage.image, &state.depthImage.allocation, nullptr));
            VkImageViewCreateInfo viewCI{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = state.depthImage.image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = state.depthImage.format,
                .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1 }
            };
            CHK(vkCreateImageView(state.device, &viewCI, nullptr, &state.depthImage.view));
        }

        // Wait on fence
        CHK(vkWaitForFences(state.device, 1, &fences[frameIndex], true, UINT64_MAX));
        CHK(vkResetFences(state.device, 1, &fences[frameIndex]));

        // Acquire next image
        auto imageAcquireRes = vkAcquireNextImageKHR(state.device, state.swapchain.swapchain, UINT64_MAX, presentSemaphores[frameIndex], VK_NULL_HANDLE, &imageIndex);
        if(imageAcquireRes == VK_ERROR_OUT_OF_DATE_KHR)
        {
            shouldResize = true;
            // continue;
        } else if(imageAcquireRes != VK_SUCCESS)
        {
            CHK(imageAcquireRes);
        }

        // Update shader data
        cameraController.update(sReg, deltatime);
        shaderData.projection = camera.projMat;
        shaderData.view = camera.viewMat;
        for (auto i = 0; i < 3; i++) {
            auto instancePos = glm::vec3((float)(i - 1) * 3.0f, 0.0f, 0.0f);
            shaderData.model[i] = glm::translate(glm::mat4(1.0f), instancePos) * glm::mat4_cast(glm::quat(glm::vec3{static_cast<float>(i*1234%14127), static_cast<float>(i*2972%91248), static_cast<float>(i*4124%87322)}));
        }
        std::memcpy(shaderDataBuffers[frameIndex].mapped, &shaderData, sizeof(ShaderUniformData));

        // Record command buffer
        auto cb = commandBuffers[frameIndex];
        CHK(vkResetCommandBuffer(cb, 0));

        VkCommandBufferBeginInfo cbBI{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        CHK(vkBeginCommandBuffer(cb, &cbBI));

        std::array<VkImageMemoryBarrier2, 2> outputBarriers{
            VkImageMemoryBarrier2{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = 0,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .image = state.swapchain.images[imageIndex],
                .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
            },
            VkImageMemoryBarrier2{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .image = state.depthImage.image,
                .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1, .layerCount = 1 }
            }
        };
        VkDependencyInfo barrierDependencyInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 2,
            .pImageMemoryBarriers = outputBarriers.data()
        };
        vkCmdPipelineBarrier2(cb, &barrierDependencyInfo);

        VkRenderingAttachmentInfo colorAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = state.swapchain.imageViews[imageIndex],
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue{.color{{ 0.0f, 0.4f, 0.0f, 1.0f }}}
        };
        VkRenderingAttachmentInfo depthAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = state.depthImage.view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = {.depthStencil = {1.0f,  0}}
        };

        VkRenderingInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = { 0, 0 },
                .extent = windowExtent,
            },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo,
            .pDepthAttachment = &depthAttachmentInfo
        };

        vkCmdBeginRendering(cb, &renderingInfo);

        VkViewport vp{
            .width = static_cast<float>(mainWindow.size.x),
            .height = static_cast<float>(mainWindow.size.y),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D scissor{ .extent{ .width = mainWindow.size.x, .height = mainWindow.size.y } };
        vkCmdSetScissor(cb, 0, 1, &scissor);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);
        VkDeviceSize vOffset{ 0 };
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipelineLayout, 0, 1, &state.descriptorSetTex, 0, nullptr);
        vkCmdBindVertexBuffers(cb, 0, 1, &mesh.buffers.pos .buffer, &vOffset);
        vkCmdBindVertexBuffers(cb, 1, 1, &mesh.buffers.uv  .buffer, &vOffset);
        vkCmdBindVertexBuffers(cb, 2, 1, &mesh.buffers.norm.buffer, &vOffset);
        vkCmdBindVertexBuffers(cb, 3, 1, &mesh.buffers.tan .buffer, &vOffset);
        vkCmdBindIndexBuffer(cb, mesh.buffers.idx.buffer, 0, VK_INDEX_TYPE_UINT32);

        vkCmdPushConstants(
            cb,
            state.pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(VkDeviceAddress),
            &shaderDataBuffers[frameIndex].deviceAddress
        );

        vkCmdDrawIndexed(cb, mesh.indexCount, 3, 0, 0, 0);

        vkCmdEndRendering(cb);

        VkImageMemoryBarrier2 barrierPresent{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = state.swapchain.images[imageIndex],
            .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
        };
        VkDependencyInfo barrierPresentDependencyInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrierPresent
        };
        vkCmdPipelineBarrier2(cb, &barrierPresentDependencyInfo);

        vkEndCommandBuffer(cb);

        // Submit command buffer
        VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &presentSemaphores[frameIndex],
            .pWaitDstStageMask = &waitStages,
            .commandBufferCount = 1,
            .pCommandBuffers = &cb,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &renderSemaphores[imageIndex],
        };
        CHK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, fences[frameIndex]));

        frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
        
        // Present image
        VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderSemaphores[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &state.swapchain.swapchain,
            .pImageIndices = &imageIndex
        };
        CHK(vkQueuePresentKHR(presentQueue, &presentInfo));
        deltatime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start).count() * 1e-9f;
    }

    CHK(vkDeviceWaitIdle(state.device));

// === === === === === === === === === === === === === === === ===

    for(auto e : sReg.view<VulkanMesh>())
    {
        auto &mesh = sReg.get<VulkanMesh>(e);
        vmaDestroyBuffer(state.vma, mesh.buffers.pos .buffer, mesh.buffers.pos .allocation);
        vmaDestroyBuffer(state.vma, mesh.buffers.uv  .buffer, mesh.buffers.uv  .allocation);
        vmaDestroyBuffer(state.vma, mesh.buffers.norm.buffer, mesh.buffers.norm.allocation);
        vmaDestroyBuffer(state.vma, mesh.buffers.tan .buffer, mesh.buffers.tan .allocation);
        vmaDestroyBuffer(state.vma, mesh.buffers.idx .buffer, mesh.buffers.idx .allocation);

        vmaDestroyImage(state.vma, mesh.textures.albedo      .image, mesh.textures.albedo      .allocation);
        vmaDestroyImage(state.vma, mesh.textures.metallic    .image, mesh.textures.metallic    .allocation);
        vmaDestroyImage(state.vma, mesh.textures.roughness   .image, mesh.textures.roughness   .allocation);
        vmaDestroyImage(state.vma, mesh.textures.ambient     .image, mesh.textures.ambient     .allocation);
        vmaDestroyImage(state.vma, mesh.textures.normal      .image, mesh.textures.normal      .allocation);
        vmaDestroyImage(state.vma, mesh.textures.displacement.image, mesh.textures.displacement.allocation);

        vkDestroyImageView(state.device, mesh.textures.albedo      .view, ALLOCATOR_HERE);
        vkDestroyImageView(state.device, mesh.textures.metallic    .view, ALLOCATOR_HERE);
        vkDestroyImageView(state.device, mesh.textures.roughness   .view, ALLOCATOR_HERE);
        vkDestroyImageView(state.device, mesh.textures.ambient     .view, ALLOCATOR_HERE);
        vkDestroyImageView(state.device, mesh.textures.normal      .view, ALLOCATOR_HERE);
        vkDestroyImageView(state.device, mesh.textures.displacement.view, ALLOCATOR_HERE);

        vkDestroySampler(state.device, mesh.textures.albedo      .sampler, ALLOCATOR_HERE);
        vkDestroySampler(state.device, mesh.textures.metallic    .sampler, ALLOCATOR_HERE);
        vkDestroySampler(state.device, mesh.textures.roughness   .sampler, ALLOCATOR_HERE);
        vkDestroySampler(state.device, mesh.textures.ambient     .sampler, ALLOCATOR_HERE);
        vkDestroySampler(state.device, mesh.textures.normal      .sampler, ALLOCATOR_HERE);
        vkDestroySampler(state.device, mesh.textures.displacement.sampler, ALLOCATOR_HERE);
    }

    vkDestroyImageView(state.device, state.depthImage.view, ALLOCATOR_HERE);
    vmaDestroyImage(state.vma, state.depthImage.image, state.depthImage.allocation);

    vkDestroyShaderModule(state.device, shaderModule, ALLOCATOR_HERE);

    for(uint i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) 
    {
        vkDestroySemaphore(state.device, presentSemaphores[i], ALLOCATOR_HERE);
        vkDestroyFence(state.device, fences[i], ALLOCATOR_HERE);
        vmaUnmapMemory(state.vma, shaderDataBuffers[i].allocation);
        vmaDestroyBuffer(state.vma, shaderDataBuffers[i].buffer, shaderDataBuffers[i].allocation);
    }

    for(uint i = 0; i < state.swapchain.imageCount; ++i)
    {
        vkDestroyImageView(state.device, state.swapchain.imageViews[i], ALLOCATOR_HERE);
        vkDestroySemaphore(state.device, renderSemaphores[i], ALLOCATOR_HERE);
    }

    vkDestroyCommandPool(state.device, state.commandPool, ALLOCATOR_HERE);

    vkDestroyDescriptorSetLayout(state.device, state.descriptorSetLayoutTex, ALLOCATOR_HERE);
    vkDestroyDescriptorPool(state.device, state.descriptorPoolTex, ALLOCATOR_HERE);
    vkDestroyPipelineLayout(state.device, state.pipelineLayout, ALLOCATOR_HERE);
    vkDestroyPipeline(state.device, state.pipeline, ALLOCATOR_HERE);
    vkDestroyRenderPass(state.device, state.renderPass, ALLOCATOR_HERE);
    // vkDestroyPipelineLayout(state.device, pipelineLayout, ALLOCATOR_HERE);
    // vkDestroyShaderModule(state.device, fragShaderModule, ALLOCATOR_HERE);
    // vkDestroyShaderModule(state.device, vertShaderModule, ALLOCATOR_HERE);

    vkDestroySwapchainKHR(state.device, state.swapchain.swapchain, ALLOCATOR_HERE);
    vmaDestroyAllocator(state.vma);
    vkDestroyDevice(state.device, ALLOCATOR_HERE);
    
    vkDestroySurfaceKHR(state.instance, state.surface, ALLOCATOR_HERE);
    if constexpr(ENABLE_VALIDATION_LAYERS)
        vkDestroyDebugUtilsMessengerEXT(state.instance, debugMessenger, ALLOCATOR_HERE);

    vkDestroyInstance(state.instance, ALLOCATOR_HERE);
    glfwDestroyWindow(mainWindow.handle);
    glfwTerminate();

    LOG_INFO("Exiting...");
}
