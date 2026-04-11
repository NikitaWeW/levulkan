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

#include "glm/glm.hpp" // Vector types, TODO: get rid of them
#include "ECS.hpp" // SparseSet
#include "resource/Resources.hpp" // Texture struct

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
    GLFWwindow *window = VK_NULL_HANDLE; ///< The window handle. 
    uint32_t version = VK_API_VERSION_1_3; ///< Vulkan api version,
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

struct AllocationCreateInfo
{
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VmaPool pool = VK_NULL_HANDLE;
    VkMemoryAllocateFlags allocFlags = 0;
    VkMemoryPropertyFlags requiredFlags = 0;
    VkMemoryPropertyFlags preferredFlags = 0;
};

struct BufferCreateInfo
{
    VkBufferUsageFlags usage = 0;
    VkBufferCreateFlags createFlags = 0;
    AllocationCreateInfo allocInfo;
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    void const *data = nullptr; ///< If not nullptr, appropriate memory flags are added automatically and the data is copied to mapped location.
    uint32_t size = 0; // In bytes
    bool map = true; ///< Map if appropriate flags are set or data is written.
};

/// @brief A buffer data allocated on the gpu
struct Buffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkBufferCreateInfo createInfo;
    VmaAllocationCreateInfo allocationInfo;

    VkDeviceAddress deviceAddress = 0;
    void *mapped = VK_NULL_HANDLE;

    bool valid();
};

Buffer makeBuffer(BufferCreateInfo const &ci);
void resizeBuffer(Buffer &buffer);

template<typename T>
inline Buffer makeBuffer(VmaAllocator allocator, T const &obj, VkBufferUsageFlags usage)
{
    return makeBuffer(BufferCreateInfo{
        .usage = usage,
        .allocInfo = {
            .allocator = allocator,
        },
        .data = &obj,
        .size = sizeof(T),
    });
}
template<typename T>
inline Buffer makeBuffer(VmaAllocator allocator, std::vector<T> const &vec, VkBufferUsageFlags usage)
{
    return makeBuffer(BufferCreateInfo{
        .allocator = allocator,
        .usage = usage,
        .data = vec.data(),
        .size = vec.size() * sizeof(T),
    });
}

/// @brief The image allocated on the gpu
struct Image
{
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    /// The transfer buffer.
    /// Free anytime after submitting the command buffer.
    Buffer srcBuffer;
    
    VkImageCreateInfo createInfo;
    VmaAllocationCreateInfo allocationInfo;
    
    VkImageUsageFlags usage = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;

    /// @brief A small helper function that checks if necessary members handles are not null
    bool valid(); 
};
struct ImageCreateInfo
{
    /// VK_IMAGE_USAGE_TRANSFER_XXX_BIT is added automatically if data is not nullptr.
    /// VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER or VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE will create the sampler.
    VkImageUsageFlags usage = 0; 
    AllocationCreateInfo allocInfo;
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Command buffer to record transfer commands to
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    
    VkFormat format = VK_FORMAT_UNDEFINED;
    struct Dimensions {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    } dimensions;
    struct Sampler {
        VkSamplerCreateFlags flags = 0;
        VkFilter magFilter = VK_FILTER_NEAREST;
        VkFilter minFilter = VK_FILTER_NEAREST;
        VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        float mipLodBias = 0;
        bool anisotropyEnable = true;
        float maxAnisotropy = 8;
        bool compareEnable = false;
        VkCompareOp compareOp;
        float minLod = 0;
        VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        bool unnormalizedCoordinates = false;
    } sampler;
    void const *data = nullptr;
};

Image makeImage(ImageCreateInfo const &ci);

struct SwapchainCreateInfo
{
    VkExtent2D size = {0, 0};
};
struct Swapchain
{
    VkSwapchainKHR swapchain;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;

    VkSwapchainCreateInfoKHR createInfo;

    struct SupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
        VkSurfaceFormatKHR surfaceFormat;
        VkPresentModeKHR surfacePresentMode;
    } swapchainSupport;
    
    uint32_t imageCount;
};
Swapchain makeSwapchain(SwapchainCreateInfo const &ci);
void resizeSwapchain(Swapchain &swapchain);

/// @brief The vulkan pipeline.
struct Pipeline
{
    enum class Type { INVALID = 0, GRAPHICS, COMPUTE, RAYTRACING };
    struct DescriptorBinding
    {
        uint32_t set = 0;
        uint32_t binding = 0;
        auto operator<=>(DescriptorBinding const &other) const = default;
    };
    struct DescriptorResource
    {
        Buffer buffer; // One large buffer for array descriptors
    };
    struct Layout
    {
        VkPipelineLayout layout;
        std::vector<VkDescriptorSetLayout> descLayouts;
        std::vector<SparseSet<VkDescriptorSet>> descSets; ///< Per frame in flight
        VkDescriptorPool descPool;
    };

    Type type = Type::INVALID;
    bool valid = false;

    // Owning
    VkPipeline pipeline;
    Layout layout;
    std::vector<std::map<Pipeline::DescriptorBinding, Pipeline::DescriptorResource>> descResources; ///< Descriptor resources allocated automatically. One per frame in flight.

    // Not owning
    VkDevice device;
    VmaAllocator allocator;
};
struct PipelineLayoutCreateInfo
{
    struct DescriptorWrite
    {
        uint32_t dstFrame = 0;
        uint32_t dstArrayElement = 0;

        // One of
        std::vector<VkDescriptorImageInfo> imageInfo;
        std::vector<VkDescriptorBufferInfo> bufferInfo;
        std::vector<VkBufferView> texelBufferView;

        inline uint32_t size() const { return std::max(imageInfo.size(), std::max(bufferInfo.size(), texelBufferView.size())); }
    };

    std::map<uint32_t, VkDescriptorSetLayoutCreateFlags> descriptorSetFlags; ///< Optional flags for descriptor sets.
    std::map<Pipeline::DescriptorBinding, VkDescriptorBindingFlags> descriptorBindingFlags; ///< Optional additional flags for descriptor bindings. VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT is set automatically.s
    std::map<Pipeline::DescriptorBinding, DescriptorWrite> descriptorWrites; ///< Descriptor data for static descriptors. If no write, creates the resource for each frame in flight.

    uint32_t maxVariableCountSize = 100;
    uint32_t maxDescriptorSets = 16;
    uint32_t framesInFlight = 1; ///< Used to determine the descriptor count
};
struct GraphicsPipelineCreateInfo
{
    PipelineLayoutCreateInfo layout; ///< Pipeline layout create info
    std::vector<VkDynamicState> dynamicState; ///< Dynamic state to enable.
    VmaAllocator allocator;

    // Vertex inputs
    struct {
        std::vector<VkVertexInputBindingDescription> bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        bool primitiveRestart = false;
    } input;

    // Attachment formats
    struct {
        std::vector<VkFormat> color;
        VkFormat depth = VK_FORMAT_UNDEFINED;
        VkFormat stencil = VK_FORMAT_UNDEFINED;
    } attachments;

    // Other state
    VkPipelineDepthStencilStateCreateInfo depthStencil;
    VkPipelineRasterizationStateCreateInfo rasterization;
    VkPipelineMultisampleStateCreateInfo multisample;
    struct { 
        std::vector<VkViewport> viewports;
        std::vector<VkRect2D> scissors;
    } viewport;
    struct {
        bool logicOpEnable;
        VkLogicOp logicOp;
        std::vector<VkPipelineColorBlendAttachmentState> attachments;
        glm::vec4 constant;
    } blending;
};

/// @brief Make a pipeline based on the shader reflection and other stuff.
Pipeline makePipeline(Shader const &shader, GraphicsPipelineCreateInfo const &ci);

// TODO: Render graph

void destroy(Swapchain &pipeline);
void destroy(Shader &shader);
void destroy(Pipeline &pipeline);
void destroy(Image &image);
void destroy(Buffer &buffer);

}; // namespace vk
