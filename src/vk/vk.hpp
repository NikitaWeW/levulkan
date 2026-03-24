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
    struct DeviceFeatures {
        VkPhysicalDeviceFeatures         features;
        VkPhysicalDeviceVulkan11Features vulkan11;
        VkPhysicalDeviceVulkan12Features vulkan12;
        VkPhysicalDeviceVulkan13Features vulkan13;
        VkPhysicalDeviceVulkan14Features vulkan14;
    } deviceFeatures; ///< Required device features. No need to set sType of pNext

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

struct ShaderCreateInfo
{
    std::string src; ///< The path to the source file. Can be empty to disable shader compilation.
    std::string bin; ///< The path to the binary root directory. Can be empty to disable writing and collecting shader binaries.
    VkDevice device = VK_NULL_HANDLE; ///< The logical device. Leave null to not create shader modules.
    std::vector<std::string> includeDirs; ///< Local ("") include directories. First most relevant. Source directory added implicitly.
    std::vector<std::string> systemIncludeDirs; ///< System ("") include directories. First most relevant.
    std::vector<std::pair<std::string, std::string>> definitions; ///< Preprocessor definitions.
    bool debugInfo = true;
};

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

    bool valid = false;
    std::vector<Binary> binaries;
    std::string source;
    ShaderCreateInfo createInfo;
};

/// @brief Make a shader program from the file.
/// Parser splits shaders stages using #shader directive
/// #stage all will append the block to all the defined stages.
/// At the beginning of the source the the stage is implicitly "#stage all"
/// For a full list of valid stage names look into Shader.cpp
/// @returns Shader with valid flag set to true if successful.
Shader makeShader(ShaderCreateInfo const &ci);

/// @brief The image allocated on the gpu
struct Image
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
struct Buffer
{
    VmaAllocation allocation;
    VkBuffer buffer;
    size_t size = 0;
    VkDeviceAddress deviceAddress = 0;
    void *mapped = nullptr;
};

struct SwapchainCreateInfo
{

};
struct Swapchain
{
    SwapchainCreateInfo createInfo;
};
// TODO
Swapchain makeSwapchain(SwapchainCreateInfo const &ci);

/// @brief The vulkan pipeline.
struct Pipeline
{
    enum class Type { INVALID, GRAPHICS, COMPUTE, RAYTRACING };
    struct DescriptorBinding
    {
        uint32_t set = 0;
        uint32_t binding = 0;
        auto operator<=>(DescriptorBinding const &other) const = default;
    };

    Type type = Type::INVALID;
    bool valid = false;

    // Owning
    VkPipeline pipeline;
    std::vector<VkDescriptorSetLayout> descLayouts;
    std::vector<SparseSet<VkDescriptorSet>> descSets; // one per frame
    VkDescriptorPool descPool;
    struct {
        std::vector<Image> colorAttachments;
        Image depthAttachment;
    } graphics;

    // Not owning
    VkDevice device;
    std::map<DescriptorBinding, Image> images;
    std::map<DescriptorBinding, Buffer> buffers;
};
struct PipelineCreateInfo
{
    struct DescriptorWrite
    {
        Pipeline::DescriptorBinding binding;
        uint32_t dstArrayElement = 0;
        uint32_t count = 1;

        // One of
        std::vector<VkDescriptorImageInfo> imageInfo;
        std::vector<VkDescriptorBufferInfo> bufferInfo;
        std::vector<VkBufferView> texelBufferView;
    };

    Pipeline::Type type = Pipeline::Type::INVALID;

    // Descriptors
    std::vector<VkDynamicState> dynamicState; ///< Dynamic state to enable. VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR are enabled automatically.
    std::map<Pipeline::DescriptorBinding, Image> images; ///< Image resources corresponding to the shader resource name.
    std::map<Pipeline::DescriptorBinding, Buffer> buffers; ///< Buffer resources corresponding to the shader resource name.
    std::map<uint32_t, VkDescriptorSetLayoutCreateFlags> descriptorSetFlags; ///< Optional flags for descriptor sets.
    std::map<Pipeline::DescriptorBinding, VkDescriptorBindingFlags> descriptorBindingFlags; ///< Optional additional flags for descriptor bindings. VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT is set automatically.s
    std::vector<DescriptorWrite> descriptorWrites;

    // Vertex inputs
    std::vector<VkVertexInputBindingDescription> vertexInputBindings;
    std::vector<VkVertexInputAttributeDescription> vertexInputAttributes;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool primitiveRestart = false;

    // Limits
    uint32_t maxVariableCountSize = 100;
    uint32_t maxDescriptorSets = 100;
    uint32_t framesInFlight = 3; ///< Used to determine the descriptor count

    struct {
        std::vector<VkFormat> colorFormats;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        VkFormat stencilFormat = VK_FORMAT_UNDEFINED;
        uint32_t viewportCount = 1;
        uint32_t scissorCount = 1;
        struct DepthStencil
        {
            bool depthTestEnable = true;
            bool depthWriteEnable = true;
            VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            bool depthBoundsTestEnable = false;
            bool stencilTestEnable = false;
            VkStencilOpState front = {};
            VkStencilOpState back = {};
            float minDepthBounds = 0;
            float maxDepthBounds = 1;
        };
    } graphics; ///< Graphics pipeline settings.
    struct {
        // empty
    } compute; ///< Compute pipeline settings.
    struct {
        uint32_t maxPipelineRayRecursionDepth = 1;
    } raytracing; ///< Raytracing pipeline settings.
};

/// @brief Make a pipeline based on the shader reflection and other stuff.
Pipeline makePipeline(Shader const &shader, PipelineCreateInfo const &ci);

// TODO: Render graph

/// @brief The model allocated ons the gpu
struct VulkanModel
{
    struct Mesh 
    {
        struct Textures
        {
            Image albedo;
            Image metallic;
            Image roughness;
            Image ambient;
            Image normal;
            Image displacement;
        } textures;
        struct Buffers
        {
            Buffer pos;
            Buffer uv;
            Buffer norm;
            Buffer tan;
            Buffer idx;
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

// TODO: render graph

void destroy(Swapchain &pipeline);
void destroy(Shader &shader);
void destroy(Pipeline &pipeline);
void destroy(VulkanModel &model);
void destroy(Image &image);
void destroy(Buffer &buffer);

}; // namespace vk
