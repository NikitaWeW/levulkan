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
#define VMA_VULKAN_VERSION 1003000
#include "vk_mem_alloc.h"
#include "GLFW/glfw3.h"

#include "Logging.hpp"
#include "Model.hpp"
#include "Loaders.hpp"

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
    VmaAllocation allocation;
    VkImage image;
    VkImageView view;
    VkSampler sampler;

    uint numMipLevels = 0;
    uint index;
};
struct BufferAllocation
{
    VmaAllocation allocation;
    VkBuffer buffer;
    size_t size;
    void *mapped = nullptr;
};
struct VulkanMesh
{
    BufferAllocation geometry;
    Mesh meshData;
    struct Textures
    {
        ImageAllocation albedo;
        ImageAllocation metallic;
        ImageAllocation roughness;
        ImageAllocation ambient;
        ImageAllocation normal;
        ImageAllocation displacement;
    } textures;
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
    VkSwapchainKHR swapchain;
    VkRenderPass renderPass;
    VkPipeline pipeline;
    VkCommandPool commandPool;

    std::vector<VkDescriptorImageInfo> textureDescriptorInfos;
    VkDescriptorPool descriptorPoolTex;

    SwapchainSupportDetails swapchainSupport;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    ImageAllocation depthImage;
    std::vector<VkFramebuffer> swapchainFramebuffers;
};

static ecs::registry sReg;

#define ALLOCATOR_HERE VK_NULL_HANDLE
#define CHK(x) { VkResult _result = x; if(_result != VK_SUCCESS) { LOG_ERROR("Failed to {}: {}", #x, string_VkResult(_result)); abort(); }}

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;
constexpr bool ENABLE_VALIDATION_LAYERS = true;
constexpr std::array<char const *, 1> sInstanceExtensions = {
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME
};
constexpr std::array<char const *, 1> sDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

struct Window
{
    glm::uvec2 size;
    GLFWwindow *handle;
};
struct EventListener
{
    struct KeyEvent
    {
        GLFWwindow *window;
        int key;
        int scancode;
        int action;
        int mods;
    };
    std::queue<KeyEvent> keyEvents;
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

static std::vector<char const *> getRequiredExtensions()
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    auto extensions = std::vector<char const *>(glfwExtensions, glfwExtensions + glfwExtensionCount);

    extensions.insert(extensions.end(), sInstanceExtensions.begin(), sInstanceExtensions.end());

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

    auto format = fmt::runtime("{} {} message:\n{}");
    if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        LOG_ERROR(format, type, severity, pCallbackData->pMessage);
    else if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        LOG_WARN(format, type, severity, pCallbackData->pMessage);
    else if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        LOG_INFO(format, type, severity, pCallbackData->pMessage);
    else
        LOG_TRACE(format, type, severity, pCallbackData->pMessage);

    return VK_FALSE;
}

static std::pair<VkInstance, bool> createInstance()
{
    VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "levulkan",
        .apiVersion = VK_API_VERSION_1_3
    };

    auto extensions = getRequiredExtensions();
    LOG_INFO("Instance extensions: {}", extensions);

    std::vector<char const *> layers;
    if(ENABLE_VALIDATION_LAYERS)
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
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
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
static VkQueue getQueue(VkDevice dev, std::optional<uint32_t> index)
{
    VkQueue queue;
    vkGetDeviceQueue(dev, index.value(), 0, &queue);
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
static bool createSwapchain(VulkanState &state, Window const &window)
{
    SwapchainSupportDetails swapchainSupport = getSwapchainSupport(state.physicalDevice, state);
    VkExtent2D extent = chooseExtent(swapchainSupport, window);

    uint32_t imageCount = swapchainSupport.capabilities.minImageCount + 1;
    imageCount = glm::clamp<unsigned>(imageCount, glm::min<unsigned>(2, swapchainSupport.capabilities.minImageCount), glm::max<unsigned>(2, swapchainSupport.capabilities.minImageCount));
    if(swapchainSupport.capabilities.maxImageCount > 0)
        imageCount = glm::min<unsigned>(imageCount, swapchainSupport.capabilities.maxImageCount);

    VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = state.surface,
        .minImageCount = imageCount,
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
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = state.queueFamilies.uniqueFamilies.size();
        createInfo.pQueueFamilyIndices = state.queueFamilies.uniqueFamilies.dense().data();
    } else {
        LOG_TRACE("VK_SHARING_MODE_EXCLUSIVE");
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    auto res = vkCreateSwapchainKHR(state.device, &createInfo, ALLOCATOR_HERE, &state.swapchain);
    if(res != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create swap chain: {}", string_VkResult(res));
        return false;
    }

    return true;
}
static void getSwapchainImages(VulkanState &state)
{
    uint32_t imageCount;
    vkGetSwapchainImagesKHR(state.device, state.swapchain, &imageCount, nullptr);
    state.swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(state.device, state.swapchain, &imageCount, state.swapchainImages.data());
    state.swapchainImageViews.resize(imageCount);
    
    for(size_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = state.swapchainImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = state.swapchainSupport.surfaceFormat.format,
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

        auto res = vkCreateImageView(state.device, &createInfo, ALLOCATOR_HERE, &state.swapchainImageViews[i]);
        if(res != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create an image view: {}", string_VkResult(res));
        }
    }
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
void createFramebuffers(VulkanState &state, VkExtent2D extent)
{
    state.swapchainFramebuffers.resize(state.swapchainImageViews.size());
    for(size_t i = 0; i < state.swapchainImageViews.size(); i++) 
    {
        std::array<VkImageView, 1> attachments{
            state.swapchainImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = state.renderPass,
            .attachmentCount = attachments.size(),
            .pAttachments = attachments.data(),
            .width = extent.width,
            .height = extent.height,
            .layers = 1,
        };

        CHK(vkCreateFramebuffer(state.device, &framebufferInfo, ALLOCATOR_HERE, &state.swapchainFramebuffers[i]));
    }
}
void recordCommandBuffer(VulkanState &state, VkCommandBuffer &commandBuffer, uint32_t imageIndex, VkExtent2D extent)
{
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = 0,
        .pInheritanceInfo = nullptr,
    };
    CHK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    VkClearValue clearColor{
        .color = {{0.0f, 0.0f, 0.0f, 1.0f}}
    };
    VkRenderPassBeginInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = state.renderPass,
        .framebuffer = state.swapchainFramebuffers[imageIndex],
        .renderArea = {
            .offset = {0, 0},
            .extent = extent
        },
        .clearValueCount = 1,
        .pClearValues = &clearColor
    };
    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);

    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = (float) extent.width,
        .height = (float) extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{
        .offset = {0, 0},
        .extent = extent,
    };
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    CHK(vkEndCommandBuffer(commandBuffer));
}

static std::string printTexture(ecs::entity e, ecs::registry const &reg)
{
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
BufferAllocation allocateMesh(VulkanState &state, Mesh const &mesh)
{
    BufferAllocation buffer;
    size_t posSize = mesh.geometry.positions.size() * sizeof(mesh.geometry.positions[0]);
    size_t texSize = mesh.geometry.texCoords.size() * sizeof(mesh.geometry.texCoords[0]);
    size_t normSize= mesh.geometry.normals.size()   * sizeof(mesh.geometry.normals[0]);
    size_t tanSize = mesh.geometry.tangents.size()  * sizeof(mesh.geometry.tangents[0]);
    size_t idxSize = mesh.geometry.indices.size()   * sizeof(mesh.geometry.indices[0]);
    buffer.size = posSize + texSize + normSize + tanSize + idxSize;

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
    std::memcpy(bufferPtr, mesh.geometry.positions.data(), posSize);
    std::memcpy(static_cast<char*>(bufferPtr) + posSize, mesh.geometry.texCoords.data(), texSize);
    std::memcpy(static_cast<char*>(bufferPtr) + posSize + texSize, mesh.geometry.normals.data(), normSize);
    std::memcpy(static_cast<char*>(bufferPtr) + posSize + texSize + normSize, mesh.geometry.tangents.data(), tanSize);
    std::memcpy(static_cast<char*>(bufferPtr) + posSize + texSize + normSize + tanSize, mesh.geometry.indices.data(), idxSize);
    vmaUnmapMemory(state.vma, buffer.allocation);

    return buffer;
}
static ImageAllocation allocateTexture(VulkanState &state, Texture const &texture)
{
    assert(texture.bitmap.numComponents == 3);
    VkFormat format = texture.srgb ? VK_FORMAT_R8G8B8_SRGB : VK_FORMAT_R8G8B8_UNORM;

    VkImageCreateInfo imageCI{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {.width = texture.bitmap.size.x, .height = texture.bitmap.size.y, .depth = 1 },
        .mipLevels = texture.numMipLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    VmaAllocationCreateInfo allocCI{
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    ImageAllocation image;
    image.numMipLevels = texture.numMipLevels;
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
            .width = texture.bitmap.size.x,
            .height = texture.bitmap.size.y,
            .depth = 1,
        }
    };
    vkCmdCopyBufferToImage(commandBuffer, imgSrcBuffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

    insertImageMemoryBarrier(commandBuffer, image.image, 
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    );

    for(uint32_t i = 1; i < texture.numMipLevels; i++)
    {
        insertImageMemoryBarrier(commandBuffer, image.image,
            0,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            {VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1}
        );
        VkImageBlit2 imageBlit{
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel   = i - 1,
                .layerCount = 1,
            },
            .srcOffsets = {
                { 0, 0, 0 },
                { int32_t(texture.bitmap.size.x >> (i - 1)), int32_t(texture.bitmap.size.y >> (i - 1)), 1 }
            },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel   = i,
                .layerCount = 1,
            },
            .dstOffsets = {
                { 0, 0, 0 },
                { int32_t(texture.bitmap.size.x >> i), int32_t(texture.bitmap.size.y >> i), 1 }
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
        vkCmdBlitImage2(commandBuffer, &imageBlitInfo);

        insertImageMemoryBarrier(commandBuffer, image.image, 
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            {VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1}
        );
    }
    insertImageMemoryBarrier(commandBuffer, image.image,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, texture.numMipLevels, 0, 1}
    );

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

    VkImageViewCreateInfo texVewCI{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = texture.numMipLevels, .layerCount = 1 }
    };
    CHK(vkCreateImageView(state.device, &texVewCI, nullptr, &image.view));

    VkSamplerCreateInfo samplerCI{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 8.0f, // 8 is a widely supported value for max anisotropy
        .maxLod = (float) texture.numMipLevels,
    };
    CHK(vkCreateSampler(state.device, &samplerCI, nullptr, &image.sampler));

    image.index = state.textureDescriptorInfos.size();
    state.textureDescriptorInfos.emplace_back(VkDescriptorImageInfo{
        .sampler = image.sampler,
        .imageView = image.view,
        .imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL
    });

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
    VkDescriptorSetLayout descriptorSetLayoutTex;
    CHK(vkCreateDescriptorSetLayout(state.device, &descLayoutTexCI, nullptr, &descriptorSetLayoutTex));

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
        .pSetLayouts = &descriptorSetLayoutTex
    };
    VkDescriptorSet descriptorSetTex;
    CHK(vkAllocateDescriptorSets(state.device, &texDescSetAlloc, &descriptorSetTex));

    VkWriteDescriptorSet writeDescSet{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSetTex,
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

    if(model.meshes.size() > 1)
        LOG_WARN("Multi-mesh models are not yet supported! \"{}\"", path);
    if(model.meshes.size() == 0)
    {
        LOG_ERROR("Model \"{}\" has no meshes!", path);
        return std::nullopt;
    }

    auto const &mesh = model.meshes.at(0);
    return VulkanMesh{
        .geometry = allocateMesh(state, mesh),
        .meshData = mesh,
        .textures = {
            .albedo       = allocateTexture(state, sReg.get<Texture>(mesh.material.textures.albedo)),
            .metallic     = allocateTexture(state, sReg.get<Texture>(mesh.material.textures.metallic)),
            .roughness    = allocateTexture(state, sReg.get<Texture>(mesh.material.textures.roughness)),
            .ambient      = allocateTexture(state, sReg.get<Texture>(mesh.material.textures.ambient)),
            .normal       = allocateTexture(state, sReg.get<Texture>(mesh.material.textures.normal)),
            .displacement = allocateTexture(state, sReg.get<Texture>(mesh.material.textures.displacement)),
        }
    };
}
template<typename T>
T valueOrAbort(std::optional<T> const &o)
{
    if(!o.has_value())
        abort();

    return o.value();
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
    glfwSetWindowUserPointer(mainWindow.handle, &sReg);
    glfwSetKeyCallback(mainWindow.handle, keyCallback);

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

    VkDebugUtilsMessengerCreateInfoEXT debugMessengerCI{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        // .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = vulkanDebugCallback,
        .pUserData = nullptr
    };
    VkDebugUtilsMessengerEXT debugMessenger;
    vkCreateDebugUtilsMessengerEXT(state.instance, &debugMessengerCI, ALLOCATOR_HERE, &debugMessenger);

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

    state.swapchainSupport = getSwapchainSupport(state.physicalDevice, state);
    getSwapchainImages(state);
    VkExtent2D extent = chooseExtent(state.swapchainSupport, mainWindow);

    LOG_INFO("{} swapchain images", state.swapchainImages.size());
    assert(state.swapchainImages.size() == state.swapchainImageViews.size());

    // VkShaderModule vertShaderModule;
    // VkShaderModule fragShaderModule;
    // VkPipelineLayout pipelineLayout;
    // createPipeline(state, vertShaderModule, fragShaderModule, pipelineLayout, extent);
    
    createFramebuffers(state, extent);
    createCommandPool(state);

    sReg.create(valueOrAbort(loadModel(state, "assets/suzanne.glb", Material{
        .textures = {
            .albedo = TextureLoader{sReg}.loadFromFile("assets/wood.jpg")
        }
    })));
    sReg.create(valueOrAbort(loadModel(state, "assets/deccer_cubes/SM_Deccer_Cubes_Textured_Complex.gltf")));

    std::array<BufferAllocation, MAX_FRAMES_IN_FLIGHT> shaderDataBuffers;
    std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> commandBuffers;
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> presentSemaphores;
    struct ShaderUniformData 
    {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 model[3];
        glm::vec3 lightPos{0.0f, -10.0f, 10.0f};
        uint32_t selected{1};
    } shaderData{};

    createShaderModule(state.device, readFileBinary("shaders-bin/basic.spv"));

    for(auto i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) 
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
        CHK(vmaCreateBuffer(state.vma, &uBufferCI, &uBufferAllocCI, &shaderDataBuffers[i].buffer, &shaderDataBuffers[i].allocation, nullptr));
        CHK(vmaMapMemory(state.vma, shaderDataBuffers[i].allocation, &shaderDataBuffers[i].mapped));

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
    }

// === === === === === === === === === === === === === === === ===

/* 
    VkQueue graphicsQueue = getQueue(state.device, state.queueFamilies.graphics);
    VkQueue presentQueue = getQueue(state.device, state.queueFamilies.present);
    float deltatime = 0.01f;
    float fpsRefresh = 0;
    constexpr float fpsRefreshRate = 0.1f;
    while(!glfwWindowShouldClose(mainWindow.handle))
    {
        auto start = std::chrono::high_resolution_clock::now();
        fpsRefresh += deltatime;
        if(fpsRefresh > fpsRefreshRate)
        {
            glfwSetWindowTitle(mainWindow.handle, fmt::format("levulkan | {:.2f}ms | {:.2f} fps", deltatime * 1e3, 1 / deltatime).c_str());
            fpsRefresh = 0;
            break;
        }
        for(auto e : sReg.view<Window>())
        {
            auto &window = sReg.get<Window>(e);
            glfwGetWindowSize(window.handle, reinterpret_cast<int *>(&window.size.x), reinterpret_cast<int *>(&window.size.y));
        }
        glfwPollEvents();

        vkWaitForFences(state.device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences(state.device, 1, &inFlightFence);

        uint32_t imageIndex;
        vkAcquireNextImageKHR(state.device, state.swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

        vkResetCommandBuffer(commandBuffer, 0);
        recordCommandBuffer(state, 0, extent);

        std::array<VkSemaphore, 1> waitSemaphores{ imageAvailableSemaphore };
        std::array<VkSemaphore, 1> signalSemaphores{ renderFinishedSemaphore };
        std::array<VkPipelineStageFlags, waitSemaphores.size()> waitStages{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = waitSemaphores.size(),
            .pWaitSemaphores = waitSemaphores.data(),
            .pWaitDstStageMask = waitStages.data(),
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer,
            .signalSemaphoreCount = signalSemaphores.size(),
            .pSignalSemaphores = signalSemaphores.data(),
        };

        CHK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence))

        VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = signalSemaphores.size(),
            .pWaitSemaphores = signalSemaphores.data(),
            .swapchainCount = 1,
            .pSwapchains = &state.swapchain,
            .pImageIndices = &imageIndex,
            .pResults = nullptr
        };

        CHK(vkQueuePresentKHR(presentQueue, &presentInfo));

        deltatime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start).count() * 1e-9f;
    } 
*/

    CHK(vkDeviceWaitIdle(state.device));

// === === === === === === === === === === === === === === === ===

    for(auto e : sReg.view<VulkanMesh>())
    {
        auto &mesh = sReg.get<VulkanMesh>(e);
        vmaDestroyBuffer(state.vma, mesh.geometry.buffer, mesh.geometry.allocation);
    }
    vmaDestroyImage(state.vma, state.depthImage.image, state.depthImage.allocation);

    for(auto i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) 
    {
        vkDestroySemaphore(state.device, presentSemaphores[i], ALLOCATOR_HERE);
        vmaDestroyBuffer(state.vma, shaderDataBuffers[i].buffer, shaderDataBuffers[i].allocation);
    }
    // vkDestroySemaphore(state.device, imageAvailableSemaphore, ALLOCATOR_HERE);
    // vkDestroySemaphore(state.device, renderFinishedSemaphore, ALLOCATOR_HERE);
    // vkDestroyFence(state.device, inFlightFence, ALLOCATOR_HERE);

    vkDestroyCommandPool(state.device, state.commandPool, ALLOCATOR_HERE);

    for(auto framebuffer : state.swapchainFramebuffers)
        vkDestroyFramebuffer(state.device, framebuffer, nullptr);
    for (auto imageView : state.swapchainImageViews)
        vkDestroyImageView(state.device, imageView, ALLOCATOR_HERE);

    vkDestroyPipeline(state.device, state.pipeline, ALLOCATOR_HERE);
    vkDestroyRenderPass(state.device, state.renderPass, ALLOCATOR_HERE);
    // vkDestroyPipelineLayout(state.device, pipelineLayout, ALLOCATOR_HERE);
    // vkDestroyShaderModule(state.device, fragShaderModule, ALLOCATOR_HERE);
    // vkDestroyShaderModule(state.device, vertShaderModule, ALLOCATOR_HERE);

    vkDestroySwapchainKHR(state.device, state.swapchain, ALLOCATOR_HERE);
    vmaDestroyAllocator(state.vma);
    vkDestroyDevice(state.device, ALLOCATOR_HERE);
    
    vkDestroySurfaceKHR(state.instance, state.surface, ALLOCATOR_HERE);
    vkDestroyDebugUtilsMessengerEXT(state.instance, debugMessenger, ALLOCATOR_HERE);

    vkDestroyInstance(state.instance, ALLOCATOR_HERE);
    glfwDestroyWindow(mainWindow.handle);
    glfwTerminate();

    LOG_INFO("Exiting...");
}
