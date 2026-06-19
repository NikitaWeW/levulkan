#include "Init.hpp"
#include "Utility.hpp"
#include "Logging.hpp"
#include <set>
using namespace vk;

std::string _sChkLastFileLine;

#define nullptr nullptr

static bool createInstance(InitInfo const &info, InitResult &result)
{
    VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = info.appName.c_str(),
        .apiVersion = info.version
    };

    uint numExtensionsAvailable;
    vkEnumerateInstanceExtensionProperties(nullptr, &numExtensionsAvailable, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(numExtensionsAvailable);
    vkEnumerateInstanceExtensionProperties(nullptr, &numExtensionsAvailable, availableExtensions.data());

    std::vector<char const *> requiredExtensions;
    if(!info.offscreen)
    {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        requiredExtensions = std::vector<char const *>(glfwExtensions, glfwExtensions + glfwExtensionCount);
    }

    for(auto const &extension : requiredExtensions)
    {
        if(std::find_if(availableExtensions.begin(), availableExtensions.end(), [&](VkExtensionProperties const &p){ return std::strcmp(p.extensionName, extension) == 0; }) == availableExtensions.end())
        {
            LOG_ERROR("Required extension \"{}\" not present!", extension);
            return false;
        } else {
            result.enabledInstanceExtensions.emplace_back(extension);
        }
    }

    for(auto const &extension : info.instanceExtensions)
    {
        if(std::find_if(availableExtensions.begin(), availableExtensions.end(), [&](VkExtensionProperties const &p){ return std::strcmp(p.extensionName, extension) == 0; }) == availableExtensions.end())
        {
            LOG_WARN("Wanted extension \"{}\" not present!", extension);
        } else {
            result.enabledInstanceExtensions.emplace_back(extension);
        }
    }

    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for(auto const &layer : info.layers)
    {
        auto pos = std::find_if(availableLayers.begin(), availableLayers.end(), [&](VkLayerProperties const &p){ return std::strcmp(p.layerName, layer) == 0; });
        if(pos == availableLayers.end())
        {
            LOG_WARN("Layer not available: \"{}\"", layer);
        } else {
            result.enabledLayers.emplace_back(layer);
        }
    }
    if(result.enabledLayers.empty())
        LOG_TRACE("Validation layers disabled!");

    VkInstanceCreateInfo instanceCI{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = static_cast<uint32_t>(result.enabledLayers.size()),
        .ppEnabledLayerNames = result.enabledLayers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(result.enabledInstanceExtensions.size()),
        .ppEnabledExtensionNames = result.enabledInstanceExtensions.data(),
    };

    auto res = vkCreateInstance(&instanceCI, nullptr, &result.instance);
    if(res != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create instance: {}!", string_VkResult(res));
        return false;
    }

    volkLoadInstance(result.instance);
    
    return true;
}

template<typename C, typename P>
requires requires (P p) { { p() } -> std::convertible_to<bool>; }
static bool containsIf(C const &cont, P const &pred)
{
    return std::find_if(cont.begin(), cont.end(), pred) != cont.end();
}
template<typename E, typename C>
static bool contains(C const &cont, E const &elem)
{
    return std::find(cont.begin(), cont.end(), elem) != cont.end();
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
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

    std::string fileLine = "<unknown location>";
    #ifndef DONT_CHECK_VK_RESULT
    fileLine = _sChkLastFileLine;
    #endif
    LOG(level, "{}: vulkan {} {} message:\n{}", fileLine, type, severity, pCallbackData->pMessage);

    return VK_FALSE;
}

static bool checkDeviceExtensionSupport(VkPhysicalDevice device, std::vector<char const *> const &deviceExtensions) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for(const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    if(!requiredExtensions.empty())
        LOG_WARN("Unsupported extensions: {}", requiredExtensions);
    return requiredExtensions.empty();
}

// A bit janky but works
static void addToFamilies(QueueFamilies &indices, uint32_t i)
{
    static float priorities = 1.0f;
    indices.deviceCreateInfo[i] = VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = i,
        .queueCount = 1,
        .pQueuePriorities = &priorities
    };
    indices.uniqueFamilies[i] = i;
    ++indices.count;
}
static bool complete(QueueFamilies const &families, std::vector<VkQueueFlagBits> const &queues, bool offscreen)
{
    return (offscreen || families.presentQueue) && queues.size() == (offscreen ? families.count : families.count - 1);
}
static QueueFamilies findQueueFamilies(VkPhysicalDevice const &device, VkSurfaceKHR surface, std::vector<VkQueueFlagBits> const &queues)
{
    QueueFamilies indices;
    bool offscreen = !surface;
    
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for(uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        auto const &family = queueFamilies[i];
        
        for(auto type : queues) {
            if(type & family.queueFlags)
            {
                indices.indices[type] = i;
                addToFamilies(indices, i);
            }
        }

        if(!offscreen)
        {
            VkBool32 presentSupport;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if(presentSupport)
            {
                indices.presentQueue = i;
                addToFamilies(indices, i);
            }
        }

        if(complete(indices, queues, offscreen))
            break;
    }

    return indices;
}

VkQueue QueueFamilies::getQueue(VkQueueFlagBits type, uint32_t queueIndex) const
{
    if(!indices.contains(type)) 
        return VK_NULL_HANDLE;

    VkQueue queue;
    vkGetDeviceQueue(device, indices.at(type), queueIndex, &queue);
    return queue;
}
// Its now time, for our FEATURE presentation!.. Feacher!.. 
// Coming straight from your house! Coming straight from your house! Coming! 
// He's the one! (Coming!) The king of only!
// She's groovy, and never glooby!
// You cant get this from an egg!
// A sensation of your screen! A show that makes you scream!
// Say it with him folks!
static InitInfo::DeviceFeatures getFeaturesSupport(VkPhysicalDevice dev)
{
    InitInfo::DeviceFeatures features;

    features.vulkan14 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .pNext = nullptr
    };
    features.vulkan13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &features.vulkan14
    };
    features.vulkan12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &features.vulkan13
    };
    features.vulkan11 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &features.vulkan12
    };
    VkPhysicalDeviceFeatures2 devFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features.vulkan11
    };
    vkGetPhysicalDeviceFeatures2(dev, &devFeatures);
    features.features = devFeatures.features;

    return features;
}

/// @brief Checks if a structure that contains n bool fields of the same type has all the required bools.
/// IMPORTANT: The structure must not contain other fields or the offset must be set accordingly.
/// @param start The offset to start the comparison in bytes
template<typename T, typename B = VkBool32>
static bool checkFeaturesSupport(T const &required, T const &supported, size_t start = 0)
{
    B const *pReq = reinterpret_cast<B const *>(&required);
    B const *pSup = reinterpret_cast<B const *>(&supported);

    size_t count = sizeof(T) / sizeof(B);

    for(size_t i = start; i < count; ++i)
    {
        if(pReq[i] && !pSup[i])
            return false;
    }

    return true;
}
static bool checkFeaturesSupport(InitInfo::DeviceFeatures const &required, InitInfo::DeviceFeatures const &supported)
{
    return checkFeaturesSupport(required.features, supported.features, 0) &&
           checkFeaturesSupport(required.vulkan11, supported.vulkan11, sizeof(VkStructureType)+sizeof(void*)) && // skip sType and pNext
           checkFeaturesSupport(required.vulkan12, supported.vulkan12, sizeof(VkStructureType)+sizeof(void*)) &&
           checkFeaturesSupport(required.vulkan13, supported.vulkan13, sizeof(VkStructureType)+sizeof(void*)) &&
           checkFeaturesSupport(required.vulkan14, supported.vulkan14, sizeof(VkStructureType)+sizeof(void*));
}
static bool isDeviceSuitable(VkPhysicalDevice dev, VkSurfaceKHR surface, InitInfo const &info)
{
    bool presentSupport = false;
    if(!info.offscreen)
    {
        std::vector<VkSurfaceFormatKHR> formats;
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &formatCount, nullptr);
    
        if(formatCount != 0) {
            formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &formatCount, formats.data());
        }
        std::vector<VkPresentModeKHR> presentModes;
    
        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &presentModeCount, nullptr);
    
        if(presentModeCount != 0) {
            presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &presentModeCount, presentModes.data());
        }
        presentSupport = formats.size() > 0 && presentModes.size() > 0;
    }

    bool featureSupport = checkFeaturesSupport(info.deviceFeatures, getFeaturesSupport(dev));

    bool queueSupport = complete(findQueueFamilies(dev, surface, info.queues), info.queues, info.offscreen);
    bool extensionSupport = checkDeviceExtensionSupport(dev, info.deviceExtensions);

    bool suitable = 
        featureSupport && 
        queueSupport &&
        extensionSupport &&
        (info.offscreen || presentSupport);

    if(!suitable)
    {
        LOG_WARN("Device suitability:");
        LOG_WARN("Feature support:   {}", featureSupport);
        LOG_WARN("Queue support:     {}", queueSupport);
        LOG_WARN("Extension support: {}", extensionSupport);
        LOG_WARN("Present support:   {}", presentSupport);
    }

    return suitable;
}
static bool pickPhysicalDevice(InitInfo const &info, InitResult &result)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(result.instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(result.instance, &deviceCount, devices.data());

    if(devices.size() == 0)
        LOG_ERROR("No vulkan devices!");

    bool deviceFound = false;
    for(auto const &dev : devices)
    {
        VkPhysicalDeviceProperties2 deviceProperties{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        VkPhysicalDeviceFeatures2 deviceFeatures{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        vkGetPhysicalDeviceProperties2(dev, &deviceProperties);
        vkGetPhysicalDeviceFeatures2(dev, &deviceFeatures);

        if(!isDeviceSuitable(dev, result.surface, info))
        {
            LOG_WARN("Physical device \"{}\" is not suitable!", deviceProperties.properties.deviceName);
            continue;
        }

        result.physicalDevice = dev;
        result.enabledDeviceExtensions = info.deviceExtensions;
        deviceFound = true;
        break;
    }    

    return deviceFound;
}


static VkDevice createDevice(VkPhysicalDevice const &physicalDevice, QueueFamilies const &families, std::vector<char const *> extensions)
{
    VkPhysicalDeviceVulkan12Features enabledVk11Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = nullptr
    };
    VkPhysicalDeviceVulkan12Features enabledVk12Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &enabledVk11Features,
    };
    VkPhysicalDeviceVulkan13Features enabledVk13Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = &enabledVk12Features,
    };
    VkPhysicalDeviceVulkan14Features enabledVk14Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
        .pNext = &enabledVk13Features,
    };
    VkPhysicalDeviceFeatures2 deviceFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &enabledVk14Features
    };
    vkGetPhysicalDeviceFeatures2(physicalDevice, &deviceFeatures);
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
    volkLoadDevice(device);

    return device;
}

static void createAllocator(InitInfo const &info, InitResult &result)
{
    VmaAllocatorCreateInfo allocatorCI{
        .flags = info.allocatorFlags, 
        .physicalDevice = result.physicalDevice,
        .device = result.device,
        .instance = result.instance,
        .vulkanApiVersion = info.version
    };
    VmaVulkanFunctions functions;
    CHECK_VK_RES(vmaImportVulkanFunctionsFromVolk(&allocatorCI, &functions));
    allocatorCI.pVulkanFunctions = &functions;
    CHECK_VK_RES(vmaCreateAllocator(&allocatorCI, &result.vma));
}

InitResult vk::init(InitInfo info)
{
    InitResult result;

    if(!createInstance(info, result))
    {
        LOG_ERROR("Failed to create the instance!");
        return result;
    }

    LOG_TRACE("Instance extensions: {}", result.enabledInstanceExtensions);
    LOG_TRACE("Layers: {}", result.enabledLayers);

    if(!info.offscreen) 
    {
        LOG_TRACE("Creating window surface");
        assert(info.window);
        auto res = glfwCreateWindowSurface(result.instance, info.window, nullptr, &result.surface);
        if(res != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create window surface: {}!", string_VkResult(res));
            return result;
        }
    }

    if(contains(info.instanceExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
    {
        LOG_TRACE("Creating debug messenger");
        VkDebugUtilsMessengerCreateInfoEXT debugMessengerCI{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = info.messageSeverity,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = info.debugCallbackOverride ? info.debugCallbackOverride : debugCallback,
            .pUserData = nullptr
        };
        vkCreateDebugUtilsMessengerEXT(result.instance, &debugMessengerCI, nullptr, &result.debugMessenger);
    }

    if(!info.offscreen)
    {
        info.deviceExtensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    if(!pickPhysicalDevice(info, result))
    {
        LOG_ERROR("No suitable device found!");
        return result;
    }


    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(result.physicalDevice, &properties);
    LOG_TRACE("Physical device: \"{}\"", properties.deviceName);

    result.queueFamilies = findQueueFamilies(result.physicalDevice, result.surface, info.queues);
    result.device = createDevice(result.physicalDevice, result.queueFamilies, info.deviceExtensions);
    result.queueFamilies.device = result.device;

    LOG_TRACE("Device extensions: {}", result.enabledDeviceExtensions);

    createAllocator(info, result);

    result.success = true;
    return result;
}
