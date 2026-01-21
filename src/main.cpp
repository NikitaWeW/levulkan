#include <bits/stdc++.h>

#include "nicecs/ecs.hpp"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/fmt/bundled/ranges.h"
#include "spdlog/fmt/ostr.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtx/io.hpp"
#include "volk.h"
#include "libraries/vk_enum_string_helper.h"
#include "GLFW/glfw3.h"

static std::shared_ptr<spdlog::logger> sLogger;
#define LOG_TRACE(...) sLogger->trace(__VA_ARGS__)
#define LOG_INFO(...) sLogger->info(__VA_ARGS__)
#define LOG_WARN(...) sLogger->warn(__VA_ARGS__)
#define LOG_ERROR(...) sLogger->error(__VA_ARGS__)

template <typename T>
using SparseSet = ecs::sparse_set<T>;

#define ALLOCATOR_HERE VK_NULL_HANDLE
#define CHK(x) \
{\
    VkResult _result = x;\
    if(_result != VK_SUCCESS)\
        LOG_ERROR("Failed to {}: {}", #x, string_VkResult(_result));\
}

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
    VkSurfaceFormatKHR surfaceFormat;
    VkPresentModeKHR surfacePresentMode;
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
struct QueueFamilies
{
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;

    SparseSet<VkDeviceQueueCreateInfo> deviceCreateInfo;
    SparseSet<uint32_t> uniqueFamilies;
    uint32_t count = 0;

    inline bool isComplete() 
    { 
        return graphics.has_value() && present.has_value(); 
    }
};
struct VulkanState
{
    QueueFamilies queueFamilies;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkRenderPass renderPass;
    VkPipeline pipeline;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    
    SwapchainSupportDetails swapchainSupport;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;
};

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

constexpr bool ENABLE_VALIDATION_LAYERS = true;

static std::vector<char const *> getRequiredExtensions()
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    auto extensions = std::vector<char const *>(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if(ENABLE_VALIDATION_LAYERS)
        extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    return extensions;
}
static std::vector<char const *> getRequiredDeviceExtensions()
{
    return {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
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

    if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        LOG_ERROR("{} {} message: {}", type, severity, pCallbackData->pMessage);
    else if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        LOG_WARN("{} {} message: {}", type, severity, pCallbackData->pMessage);
    else if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        LOG_INFO("{} {} message: {}", type, severity, pCallbackData->pMessage);
    else
        LOG_TRACE("{} {} message: {}", type, severity, pCallbackData->pMessage);

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
    LOG_INFO("Extensions: {}", extensions);

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
static bool init()
{
    spdlog::set_pattern("%^[%T] %v%$");
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

    return device;
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
    imageCount = glm::max<unsigned>(imageCount, 2);
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
static void createPipeline(VulkanState &state,
    VkShaderModule &vertShaderModule,
    VkShaderModule &fragShaderModule,
    VkPipelineLayout &pipelineLayout,
    VkExtent2D extent)
{
    auto vertShaderCode = readFileBinary("shaders-bin/vert.vert.spv");
    auto fragShaderCode = readFileBinary("shaders-bin/frag.frag.spv");

    vertShaderModule = createShaderModule(state.device, vertShaderCode);
    fragShaderModule = createShaderModule(state.device, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertShaderModule,
        .pName = "main",
        .pSpecializationInfo = nullptr
    };
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragShaderModule,
        .pName = "main",
        .pSpecializationInfo = nullptr
    };

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0,
        .pVertexAttributeDescriptions = nullptr,
    };

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = (float) extent.width,
        .height = (float) extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor{
        .offset = {0, 0},
        .extent = extent,
    };

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };

    VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo colorBlending{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
        .blendConstants = {
            0.0f, 0.0f, 0.0f, 0.0f
        }
    };

    VkAttachmentDescription colorAttachment{
        .format = state.swapchainSupport.surfaceFormat.format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference colorAttachmentRef{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef,
    };

    VkRenderPassCreateInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    
    CHK(vkCreateRenderPass(state.device, &renderPassInfo, ALLOCATOR_HERE, &state.renderPass));

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 0,
        .pSetLayouts = nullptr,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr,
    };
    CHK(vkCreatePipelineLayout(state.device, &pipelineLayoutInfo, ALLOCATOR_HERE, &pipelineLayout));

    VkGraphicsPipelineCreateInfo pipelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout,
        .renderPass = state.renderPass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    CHK(vkCreateGraphicsPipelines(state.device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, ALLOCATOR_HERE, &state.pipeline));
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
void createCommandBuffer(VulkanState &state, VkExtent2D extent)
{
    VkCommandPoolCreateInfo poolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = state.queueFamilies.graphics.value(),
    };
    CHK(vkCreateCommandPool(state.device, &poolCreateInfo, ALLOCATOR_HERE, &state.commandPool));

    VkCommandBufferAllocateInfo commandBufferAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = state.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    CHK(vkAllocateCommandBuffers(state.device, &commandBufferAllocateInfo, &state.commandBuffer));

    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = 0,
        .pInheritanceInfo = nullptr,
    };
    CHK(vkBeginCommandBuffer(state.commandBuffer, &beginInfo));

    uint32_t imageIndex = 0;
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
    vkCmdBeginRenderPass(state.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    vkCmdBindPipeline(state.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, state.pipeline);

    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = (float) extent.width,
        .height = (float) extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(state.commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{
        .offset = {0, 0},
        .extent = extent,
    };
    vkCmdSetScissor(state.commandBuffer, 0, 1, &scissor);
    vkCmdDraw(state.commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(state.commandBuffer);

    CHK(vkEndCommandBuffer(state.commandBuffer));
}

int main(int argc, char const **argv)
{
    // Non-vulkan setup
    if(!init())
    {
        LOG_ERROR("Failed to init!");
        return -1;
    }

    ecs::registry reg;
    VulkanState &state = reg.get<VulkanState>(reg.create<VulkanState>());
    Window &mainWindow = reg.get<Window>(reg.create<Window>());

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    mainWindow.handle = glfwCreateWindow(800, 600, "levulkan", nullptr, nullptr);
    glfwGetWindowSize(mainWindow.handle, reinterpret_cast<int *>(&mainWindow.size.x), reinterpret_cast<int *>(&mainWindow.size.y));
    glfwSetWindowUserPointer(mainWindow.handle, &reg);
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

    assert(state.queueFamilies.isComplete());

    if(!createSwapchain(state, mainWindow))
        return -1;

    state.swapchainSupport = getSwapchainSupport(state.physicalDevice, state);
    getSwapchainImages(state);
    VkExtent2D extent = chooseExtent(state.swapchainSupport, mainWindow);

    VkShaderModule vertShaderModule;
    VkShaderModule fragShaderModule;
    VkPipelineLayout pipelineLayout;
    
    createPipeline(state, vertShaderModule, fragShaderModule, pipelineLayout, extent);
    createFramebuffers(state, extent);
    createCommandBuffer(state, extent);

// === === === === === === === === === === === === === === === ===
    // VkQueue graphicsQueue = getQueue(state.device, state.queueFamilies.graphics);
    // VkQueue presentQueue = getQueue(state.device, state.queueFamilies.present);

    while(!glfwWindowShouldClose(mainWindow.handle))
    {
        for(auto e : reg.view<Window>())
        {
            auto &window = reg.get<Window>(e);
            glfwGetWindowSize(window.handle, reinterpret_cast<int *>(&window.size.x), reinterpret_cast<int *>(&window.size.y));
        }
        glfwPollEvents();
        break;
    }

// === === === === === === === === === === === === === === === ===

    vkDestroyCommandPool(state.device, state.commandPool, ALLOCATOR_HERE);

    for(auto framebuffer : state.swapchainFramebuffers) {
        vkDestroyFramebuffer(state.device, framebuffer, nullptr);
    }
    for (auto imageView : state.swapchainImageViews) {
        vkDestroyImageView(state.device, imageView, ALLOCATOR_HERE);
    }

    vkDestroyPipeline(state.device, state.pipeline, ALLOCATOR_HERE);
    vkDestroyRenderPass(state.device, state.renderPass, ALLOCATOR_HERE);
    vkDestroyPipelineLayout(state.device, pipelineLayout, ALLOCATOR_HERE);
    vkDestroyShaderModule(state.device, fragShaderModule, ALLOCATOR_HERE);
    vkDestroyShaderModule(state.device, vertShaderModule, ALLOCATOR_HERE);

    vkDestroySwapchainKHR(state.device, state.swapchain, ALLOCATOR_HERE);
    vkDestroyDevice(state.device, ALLOCATOR_HERE);
    
    vkDestroySurfaceKHR(state.instance, state.surface, ALLOCATOR_HERE);
    vkDestroyDebugUtilsMessengerEXT(state.instance, debugMessenger, ALLOCATOR_HERE);

    vkDestroyInstance(state.instance, ALLOCATOR_HERE);
    glfwDestroyWindow(mainWindow.handle);
    glfwTerminate();

    LOG_INFO("Exiting...");
}
