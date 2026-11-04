#pragma once
#include "engine/renderer/RenderGraphRenderer.hpp"
#include "engine/renderer/ResourceManager.hpp"
#include "engine/scene/Camera.hpp"

namespace egn {

VkPipelineColorBlendAttachmentState constexpr ALPHA_BLENDING{
    .blendEnable = true,
    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    .colorBlendOp = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp = VK_BLEND_OP_ADD,
    .colorWriteMask = 0xFF,
};
VkPipelineColorBlendAttachmentState constexpr ADDITIVE_BLENDING{
    .blendEnable = true,
    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
    .colorBlendOp = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp = VK_BLEND_OP_ADD,
    .colorWriteMask = 0xFF,
};
VkPipelineColorBlendAttachmentState constexpr NO_BLENDING{
    .blendEnable = false,
    .colorWriteMask = 0xFF,
};

egn::ResourceTraits constexpr COLOR_ATTACHMENT_TRAITS{
    .imageTraits = {
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    },
    .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    .stages = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
};
egn::ResourceTraits constexpr DEPTH_STENCIL_ATTACHMENT_TRAITS{
    .imageTraits = {
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
        .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    },
    .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    .stages = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
};
egn::ResourceTraits constexpr SHADER_READ_TRAITS{
    .imageTraits = {
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    },
    .access = VK_ACCESS_2_SHADER_READ_BIT,
    .stages = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
};
egn::ResourceTraits constexpr TRANSFER_SRC_TRAITS{
    .imageTraits = {
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    },
    .access = VK_ACCESS_2_TRANSFER_READ_BIT,
    .stages = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
};
egn::ResourceTraits constexpr TRANSFER_DST_TRAITS{
    .imageTraits = {
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    },
    .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    .stages = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
};

class EngineRenderer : protected RenderGraphRenderer {
private:
    struct EngineRendererImpl *mImpl = nullptr;
public:
    EngineRenderer(Registry &reg, vk::Device const &device);
    EngineRenderer(EngineRenderer const &) = delete;
    EngineRenderer(EngineRenderer &&) = default;
    EngineRenderer &operator=(EngineRenderer const &) = delete;
    EngineRenderer &operator=(EngineRenderer &&) = default;
    ~EngineRenderer();

    void setCamera(RestrictedEntity<Camera> camera);
    void preFrame(Registry &reg, uint frameIndex) override;
    void render(RestrictedEntity<vk::Image> target, CommandBuffers &commandBuffers) override;
};

};
