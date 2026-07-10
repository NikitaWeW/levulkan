/*
$$\    $$\ $$\   $$\   Vulkan helper functionality.
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

struct DescriptorBinding
{
    uint32_t set = 0;
    uint32_t binding = 0;
    auto operator<=>(DescriptorBinding const &other) const = default;
};

struct PipelineLayoutCreateInfo
{
    std::map<DescriptorBinding, VkDescriptorType> descriptorTypeOverride; ///< Optional overrides of the descriptor type for specific bindings. Useful to make some descriptors dynamic.
    std::map<DescriptorBinding, VkDescriptorBindingFlags> descriptorBindingFlags; ///< Optional additional flags for descriptor bindings. VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT is set automatically.s
    std::map<uint32_t, VkDescriptorSetLayoutCreateFlags> descriptorSetFlags; ///< Optional flags for descriptor sets.
    std::map<DescriptorBinding, uint32_t> unsizedDescriptorSize; ///< Self explanatory. Each unsized descriptor array must have an entry here.

    uint32_t maxVariableCountSize = 100;
    uint32_t maxDescriptorSets = 16;
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
        bool depthTestEnable = false;
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
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
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

/// @brief The vulkan pipeline.
struct Pipeline
{
    enum class Type { INVALID = 0, GRAPHICS, COMPUTE, RAYTRACING };
    struct Layout
    {
        VkPipelineLayout layout;
        SparseSet<VkDescriptorSetLayout> descLayouts;
        SparseSet<VkDescriptorSet> descSets;
        VkDescriptorPool descPool;

        void *_reflection = nullptr; // Internal
    };

    Type type = Type::INVALID;
    struct {
        GraphicsPipelineCreateInfo graphics;
        ComputePipelineCreateInfo compute;
        RaytracingPipelineCreateInfo raytracing;
    } createInfo;

    bool valid = false;

    // Owning
    VkPipeline pipeline;
    Layout layout;

    // Not owning
    VkDevice device;
    VmaAllocator allocator;
};
// FIXME: this runs on hopes and dreams
struct DescriptorWrite
{
    uint32_t dstSet = 0;
    uint32_t dstBinding = 0;
    uint32_t dstArrayElement = 0;

    // One of
    std::vector<VkDescriptorImageInfo> imageInfo;
    std::vector<VkDescriptorBufferInfo> bufferInfo;
    std::vector<VkBufferView> texelBufferView;

    inline uint32_t size() const { return std::max(imageInfo.size(), std::max(bufferInfo.size(), texelBufferView.size())); }
};

/// @brief Make a pipeline based on the shader reflection and other stuff.
Pipeline makePipeline(Shader const &shader, GraphicsPipelineCreateInfo const &ci);
/// @copydoc makePipeline
Pipeline makePipeline(Shader const &shader, ComputePipelineCreateInfo const &ci);
/// @copydoc makePipeline
Pipeline makePipeline(Shader const &shader, RaytracingPipelineCreateInfo const &ci);

void writeDescriptors(Pipeline const &pipeline, std::vector<DescriptorWrite> const &writes);

void destroy(Pipeline &pipeline);

} // namespace vk