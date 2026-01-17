#include <bits/stdc++.h>

#include "nicecs/ecs.hpp"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/fmt/bundled/ranges.h"

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
    std::optional<uint32_t> preset;

    SparseSet<VkDeviceQueueCreateInfo> deviceCreateInfo;

    inline bool isComplete() 
    { 
        return graphics.has_value() && preset.has_value(); 
    }
};
struct VulkanState
{
    QueueFamilies queueFamilies;
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkSurfaceKHR surface;
    GLFWwindow *window;
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
            indices.deviceCreateInfo[i] = makeDeviceQueueCreateInfo(i);
        }

        VkBool32 presentSupport;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, state.surface, &presentSupport);
        if(presentSupport)
        {
            indices.preset = i;
            indices.deviceCreateInfo[i] = makeDeviceQueueCreateInfo(i);
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
static bool isDeviceSuitable(VkPhysicalDevice dev, VulkanState const &state)
{
    VkPhysicalDeviceFeatures2 features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    vkGetPhysicalDeviceFeatures2(dev, &features);
    return features.features.geometryShader && findQueueFamilies(dev, state).isComplete();
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
    VkDeviceCreateInfo deviceCI{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabledVk13Features,
        .queueCreateInfoCount = static_cast<uint32_t>(families.deviceCreateInfo.data().size()),
        .pQueueCreateInfos = families.deviceCreateInfo.data().data(),
        .pEnabledFeatures = &deviceFeatures.features
    };
    VkDevice device;
    vkCreateDevice(physicalDevice, &deviceCI, nullptr, &device);

    return device;
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

    {
        auto [instance, instanceCreated] = createInstance();
        if(!instanceCreated)
            return -1;
        state.instance = instance;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    state.window = glfwCreateWindow(800, 600, "levulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(state.window, &reg);
    glfwSetKeyCallback(state.window, keyCallback);

    {
        auto res = glfwCreateWindowSurface(state.instance, state.window, nullptr, &state.surface);
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
    }

    state.queueFamilies = findQueueFamilies(state.physicalDevice, state);
    state.device = createDevice(state.physicalDevice, state.queueFamilies);

    VkQueue graphicsQueue;
    vkGetDeviceQueue(state.device, state.queueFamilies.graphics.value(), 0, &graphicsQueue);

    while(!glfwWindowShouldClose(state.window))
    {
        glfwPollEvents();
        break;
    }

    vkDestroyDevice(state.device, nullptr);
    vkDestroySurfaceKHR(state.instance, state.surface, nullptr);
    
    vkDestroyDebugUtilsMessengerEXT(state.instance, debugMessenger, nullptr);
    vkDestroyInstance(state.instance, nullptr);
    glfwDestroyWindow(state.window);
    glfwTerminate();

    LOG_INFO("Exiting...");
}
