/*
$$\    $$\ $$\   $$\   My vulkan abstraction.
$$ |   $$ |$$ | $$  |  Copyright(c) 2026 Nikita Martynau 
$$ |   $$ |$$ |$$  /   https://opensource.org/license/mit 
\$$\  $$  |$$$$$  /    insert git repo url here
 \$$\$$  / $$  $$<     
  \$$$  /  $$ |\$$\    Pipeline creation and descriptor management utility.
   \$  /   $$ | \$$\   Creates the pipeline from the shader     
    \_/    \__|  \__|  reflection and other options.
*/

#pragma once
#include "ECS.hpp" // SparseSet
#include "Resource.hpp"
#include "Shader.hpp"
#include "vulkan.h"
#include <vector>
#include <map>

namespace vk {

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
    struct Layout
    {
        VkPipelineLayout layout;
        SparseSet<VkDescriptorSetLayout> descLayouts;
        std::vector<SparseSet<VkDescriptorSet>> descSets; ///< Per frame in flight
        VkDescriptorPool descPool;
    };

    Type type = Type::INVALID;
    bool valid = false;

    // Owning
    VkPipeline pipeline;
    Layout layout;
    /// Descriptor buffers allocated automatically. 
    /// Frame -> binding
    /// Of size blockSize * descCout
    std::vector<std::map<Pipeline::DescriptorBinding, Buffer>> descResources; 

    // Not owning
    VkDevice device;
    VmaAllocator allocator;

    /// @brief Access the automatically allocated buffer.
    /// @param binding The buffer binding.
    /// @param frame The frame index.
    /// @param index The array index.
    inline Buffer const &getBuffer(Pipeline::DescriptorBinding binding, uint frame = 0) const 
    { 
        return descResources.at(frame).at(binding); 
    }
};
// FIXME: this runs on hopes and dreams
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

    std::map<Pipeline::DescriptorBinding, VkDescriptorType> descriptorTypeOverride; ///< Optional overrides of the descriptor type for specific bindings. Useful to make some descriptors dynamic.
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
    VkPipelineCreateFlags flags = 0;
    std::vector<VkDynamicState> dynamicState; ///< Dynamic state to enable.
    VmaAllocator allocator;

    struct Input {
        std::vector<VkVertexInputBindingDescription> bindings;
        std::vector<VkVertexInputAttributeDescription> attributes;
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        bool primitiveRestart = false;
    } input;
    struct Attachments {
        std::vector<VkFormat> color;
        VkFormat depth = VK_FORMAT_UNDEFINED;
        VkFormat stencil = VK_FORMAT_UNDEFINED;
    } attachments;
    struct DepthStencil {
        VkPipelineDepthStencilStateCreateFlags flags = 0;
        bool depthTestEnable = true;
        bool depthWriteEnable = true;
        VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
        bool depthBoundsTestEnable = false;
        bool stencilTestEnable = false;
        VkStencilOpState front = {};
        VkStencilOpState back = {};
        float minDepthBounds = 0;
        float maxDepthBounds = 0;
    } depthStencil;
    struct Rasterization {
        VkPipelineRasterizationStateCreateFlags flags = 0; 
        bool depthClampEnable = false;
        bool rasterizerDiscardEnable = false;
        VkPolygonMode polygonMode;
        VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
        VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        bool depthBiasEnable = false;
        float depthBiasConstantFactor = 0.0f;
        float depthBiasClamp = 0.0f;
        float depthBiasSlopeFactor = 0.0f;
        float lineWidth = 1.0f;
    } rasterization;
    struct Multisample {
        VkPipelineMultisampleStateCreateFlags flags = 0;
        VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        bool sampleShadingEnable = false;
        float minSampleShading = 0.0f;
        std::vector<VkSampleMask> sampleMask = { 1 };
        bool alphaToCoverageEnable = false;
        bool alphaToOneEnable = false;
    } multisample;
    struct Viewport { 
        uint32_t viewportCount = 1;
        uint32_t scissorCount = 1;
        std::vector<VkViewport> viewports;
        std::vector<VkRect2D> scissors;
    } viewport;
    struct Blending {
        bool logicOpEnable = false;
        VkLogicOp logicOp;
        std::vector<VkPipelineColorBlendAttachmentState> attachments;
        struct {
            float r = 0, g = 0, b = 0, a = 0;
        } constant;
    } blending;
};
struct ComputePipelineCreateInfo
{
    PipelineLayoutCreateInfo layout; ///< Pipeline layout create info
    VkPipelineCreateFlags flags = 0;
    VmaAllocator allocator = VK_NULL_HANDLE;
};
struct RaytracingPipelineCreateInfo
{
    PipelineLayoutCreateInfo layout; ///< Pipeline layout create info
    VkPipelineCreateFlags flags = 0;
    VmaAllocator allocator = VK_NULL_HANDLE;
};

/// @brief Make a pipeline based on the shader reflection and other stuff.
Pipeline makePipeline(Shader const &shader, GraphicsPipelineCreateInfo const &ci);
/// @copydoc makePipeline
Pipeline makePipeline(Shader const &shader, ComputePipelineCreateInfo const &ci);
/// @copydoc makePipeline
Pipeline makePipeline(Shader const &shader, RaytracingPipelineCreateInfo const &ci);

void destroy(Pipeline &pipeline);

} // namespace vk