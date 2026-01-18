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

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
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
    auto res = vkCreateInstance(&instanceCI, nullptr, &instance);
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
static SwapchainSupportDetails getSwapChainSupport(VkPhysicalDevice const &dev, VulkanState const &state) {
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

    return details;
}
static bool isDeviceSuitable(VkPhysicalDevice dev, VulkanState const &state)
{
    VkPhysicalDeviceFeatures2 features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    vkGetPhysicalDeviceFeatures2(dev, &features);
    auto swapChainSupport = getSwapChainSupport(dev, state);
    return 
        features.features.geometryShader && 
        findQueueFamilies(dev, state).isComplete() && 
        checkDeviceExtensionSupport(dev, getRequiredDeviceExtensions()) &&
        swapChainSupport.formats.size() > 0 && swapChainSupport.presentModes.size() > 0;
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
    vkCreateDevice(physicalDevice, &deviceCI, nullptr, &device);

    return device;
}
static VkQueue getQueue(VkDevice dev, std::optional<uint32_t> index)
{
    VkQueue queue;
    vkGetDeviceQueue(dev, index.value(), 0, &queue);
    return queue;
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
    SwapchainSupportDetails swapChainSupport = getSwapChainSupport(state.physicalDevice, state);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport);
    VkPresentModeKHR surfacePresentMode = chooseSwapPresentMode(swapChainSupport);
    VkExtent2D extent = chooseExtent(swapChainSupport, window);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    imageCount = glm::max<unsigned>(imageCount, 2);
    if(swapChainSupport.capabilities.maxImageCount > 0)
        imageCount = glm::min<unsigned>(imageCount, swapChainSupport.capabilities.maxImageCount);

    VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = state.surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = swapChainSupport.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = surfacePresentMode,
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

    auto res = vkCreateSwapchainKHR(state.device, &createInfo, nullptr, &state.swapchain);
    if(res != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create swap chain: {}", string_VkResult(res));
        return false;
    }

    return true;
}

int main(int argc, char const **argv)
{
    if(!init())
    {
        LOG_ERROR("Failed to init!");
        return -1;
    }

    ecs::registry reg;
    VulkanState &state = reg.get<VulkanState>(reg.create<VulkanState>());
    Window &mainWindow = reg.get<Window>(reg.create<Window>());

    {
        auto [instance, instanceCreated] = createInstance();
        if(!instanceCreated)
            return -1;
        state.instance = instance;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    mainWindow.handle = glfwCreateWindow(800, 600, "levulkan", nullptr, nullptr);
    glfwGetWindowSize(mainWindow.handle, reinterpret_cast<int *>(&mainWindow.size.x), reinterpret_cast<int *>(&mainWindow.size.y));
    glfwSetWindowUserPointer(mainWindow.handle, &reg);
    glfwSetKeyCallback(mainWindow.handle, keyCallback);

    {
        auto res = glfwCreateWindowSurface(state.instance, mainWindow.handle, nullptr, &state.surface);
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
    vkCreateDebugUtilsMessengerEXT(state.instance, &debugMessengerCI, nullptr, &debugMessenger);

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

    VkQueue graphicsQueue = getQueue(state.device, state.queueFamilies.graphics);
    VkQueue presentQueue = getQueue(state.device, state.queueFamilies.present);

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

    vkDestroySwapchainKHR(state.device, state.swapchain, nullptr);
    vkDestroyDevice(state.device, nullptr);
    
    vkDestroySurfaceKHR(state.instance, state.surface, nullptr);
    vkDestroyDebugUtilsMessengerEXT(state.instance, debugMessenger, nullptr);

    vkDestroyInstance(state.instance, nullptr);
    glfwDestroyWindow(mainWindow.handle);
    glfwTerminate();

    LOG_INFO("Exiting...");
}
