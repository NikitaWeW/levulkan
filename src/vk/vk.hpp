/*
$$\    $$\ $$\   $$\   My vulkan abstraction.
$$ |   $$ |$$ | $$  |  Copyright (c) 2026 Nikita Martynau 
$$ |   $$ |$$ |$$  /   https://opensource.org/license/mit 
\$$\  $$  |$$$$$  /    insert git repo url here
 \$$\$$  / $$  $$<     
  \$$$  /  $$ |\$$\    
   \$  /   $$ | \$$\   
    \_/    \__|  \__|  
*/
#pragma once

#include <bits/stdc++.h>

#include "volk.h"
#include "libraries/vk_enum_string_helper.h"
#include "vk_mem_alloc.h"
#include "GLFW/glfw3.h"

#include "resource/Model.hpp"
#include "glm/glm.hpp"
#include "ECS.hpp"


#ifndef DONT_CHECK_VK
extern std::string _sChkLastFileLine;
#define CHK(x) { _sChkLastFileLine = std::string(__FILE__) + ':' + std::to_string(__LINE__); VkResult _result = x; if(_result != VK_SUCCESS) { LOG_ERROR("{}:{}: Failed to {}: {}.", __FILE__, __LINE__, #x, string_VkResult(_result)); /* LOG_WARN("Aborting..."); abort(); */ }}
#else
#define CHK(x) x
#endif

namespace vk
{

/// Very minimal init options
/// The init utility is designed to be tweaked directly.
struct InitInfo
{
    std::string appName; ///< The name of the application.
    GLFWwindow *window = nullptr; ///< The window handle. 
    uint32_t version = VK_API_VERSION_1_3;
    bool offscreen = false; ///< Controls whether presentation is required.
    
    std::vector<char const *> instanceExtensions; ///< A list of required instance extensions excluding required extensions.
    std::vector<char const *> deviceExtensions; ///< A list of required device extensions excluding required extensions. VK_KHR_SWAPCHAIN_EXTENSION_NAME is implicitly included if offscreen is not true.
    std::vector<char const *> layers; ///< A list of required layers.
    std::vector<VkQueueFlagBits> queues = { VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_TRANSFER_BIT }; ///< A list of required queues. Present queue is searched for implicitly.

    VkDebugUtilsMessageSeverityFlagsEXT messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    PFN_vkDebugUtilsMessengerCallbackEXT debugCallbackOverride = nullptr; ///< Leave nullptr for default callback.
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

/// @brief The compiled spirv program.
struct Shader
{
    struct Binary
    {
        VkShaderStageFlagBits stage;
        std::vector<uint32_t> spirv;
        VkShaderModule module = VK_NULL_HANDLE;
        std::string path;
    };
    struct Source
    {
        std::string data;
        std::string path; 
    };

    VkDevice device = VK_NULL_HANDLE;
    bool valid = false;
    std::vector<Binary> binaries;
    Source src;
    std::string binPath;
};

/// @brief Make a shader program from the file.
/// Parser splits shaders stages using #shader directive
/// #stage all will append the block to all the defined stages.
/// At the beginning of the source the the stage is implicitly "#stage all"
/// For a full list of valid stage names look into Shader.cpp
/// @param src The path to the source file. Can be empty.
/// @param bin The path to the binary root directory.
/// @param dev The logical device. Leave null to not create shader modules.
/// @returns Shader with valid flag set to true if successful.
Shader makeShader(std::string_view src, std::string_view bin, VkDevice dev = VK_NULL_HANDLE);

/// @brief The vulkan pipeline.
struct Pipeline
{
    VkSwapchainKHR swapchain;
    VkPipeline pipeline;
    // TODO: add more stuff
};

/// @brief Make a pipeline based on the shader reflection.
Pipeline makePipeline(Shader const &shader);

/// @brief The image allocated on the gpu
struct ImageAllocation
{
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageCreateInfo imageCreateInfo;

    glm::uvec2 size;
    uint numComponents = 0;
    uint numMipLevels = 1;
};
/// @brief A buffer data allocated on the gpu
struct BufferAllocation
{
    VmaAllocation allocation;
    VkBuffer buffer;
    size_t size = 0;
    VkDeviceAddress deviceAddress = 0;
    void *mapped = nullptr;
};

/// @brief The model allocated ons the gpu
struct VulkanModel
{
    struct Mesh 
    {
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
        size_t meshIndex;
    };

    // TODO: add animation support

    Entity eModel;
    std::vector<Mesh> meshes;
};

/// @brief Allocate the model.
VulkanModel processModel(Model const &model);

void destroy(Shader &shader); ///< Destroy the shader
void destroy(Pipeline &pipeline); ///< Destroy the pipeline
void destroy(VulkanModel &model); ///< Destroy the model

}; // namespace vk
