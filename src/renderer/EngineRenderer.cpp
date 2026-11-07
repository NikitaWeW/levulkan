#include "engine/renderer/EngineRenderer.hpp"
#include "engine/scene/Camera.hpp"

using namespace egn;

struct gbuffer_data {
    RestrictedEntity<vk::Pipeline> pipeline;
    RestrictedEntity<vk::Shader> shader;

    Entity albedo;
    Entity position;
    Entity normal;
    Entity pbr;
    Entity depth;
};
struct lighting_data {
    RestrictedEntity<vk::Pipeline> pipeline;
    RestrictedEntity<vk::Shader> shader;

    Entity output;
};
struct swapchain_blit_data {
    Entity src;
    Entity target;
};

struct egn::EngineRendererImpl {
    RenderGraphRenderer *renderGraph = nullptr;
    vk::Device device;
    vk::RingBuffer uniformBuffer;
    Registry *reg = nullptr;
    uint frameIndex = 0;
    VkCommandPool commandPool;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    ResourceAllocator resourceAllocator;
    DescriptorManager descManager;
    Entity camera;

    glm::uvec2 lastTargetDimensions{ 0 };
};

static VkFormat getDepthFormat(VkPhysicalDevice dev) {
    VkFormat outFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkFormat> depthFormatList{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM };
    for(VkFormat &format : depthFormatList) {
        VkFormatProperties2 formatProperties{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
        vkGetPhysicalDeviceFormatProperties2(dev, format, &formatProperties);
        if(formatProperties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            outFormat = format;
            break;
        }
    }
    if(outFormat == VK_FORMAT_UNDEFINED) {
        LOG_ERROR("Failed to pick depth image format!");
        outFormat = depthFormatList.at(0);
    }

    return outFormat;
}
static VkCommandPool makeCommandPool(uint32_t index, VkDevice device) {
    VkCommandPoolCreateInfo commandPoolCI{ .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = index };
    VkCommandPool commandPool;
    vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool);
    return commandPool;
}
template <typename T> static uint64_t pushUniform(egn::vk::RingBuffer &uniformBuffer, T const &data, uint frame, uint alignment = 16) {
    uint64_t offset = uniformBuffer.request(sizeof(data), frame, alignment);
    std::memcpy(static_cast<char *>(uniformBuffer.getBuffer()->hostAddress) + offset, &data, sizeof(data));
    offset += uniformBuffer.getBuffer()->deviceAddress;
    return offset;
}
template <std::ranges::range T> static uint64_t pushUniform(egn::vk::RingBuffer &uniformBuffer, T const &data, uint frame, uint alignment = 16) {
    uint32_t size = std::ranges::size(data) * sizeof(*std::ranges::data(data));
    uint64_t offset = uniformBuffer.request(size, frame, alignment);
    std::memcpy(static_cast<char *>(uniformBuffer.getBuffer()->hostAddress) + offset, std::ranges::data(data), size);
    offset += uniformBuffer.getBuffer()->deviceAddress;
    return offset;
}

static void setupGBufferPass(egn::RenderPassBuilder &builder, void *pRenderer) {
    EngineRendererImpl &renderer = *static_cast<EngineRendererImpl *>(pRenderer);
    builder->addImageResource("gbuffer_albedo", {
        .imageInfo = {
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .view = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
        },
        .resizeToTarget = true,
    });
    builder->addImageResource("gbuffer_position", {
        .imageInfo = {
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .view = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
        },
        .resizeToTarget = true,
    });
    builder->addImageResource("gbuffer_normal", {
        .imageInfo = {
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .view = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
        },
        .resizeToTarget = true,
    });
    builder->addImageResource("gbuffer_pbr", {
        .imageInfo = {
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .view = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
        },
        .resizeToTarget = true,
    });
    builder->addImageResource("gbuffer_depth", { 
        .imageInfo = { 
            .format = renderer.depthFormat,
            .view = { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT }, 
        },
        .resizeToTarget = true, 
    });

    builder.attachResourceWrite("gbuffer_albedo", egn::COLOR_ATTACHMENT_TRAITS);
    builder.attachResourceWrite("gbuffer_position", egn::COLOR_ATTACHMENT_TRAITS);
    builder.attachResourceWrite("gbuffer_normal", egn::COLOR_ATTACHMENT_TRAITS);
    builder.attachResourceWrite("gbuffer_pbr", egn::COLOR_ATTACHMENT_TRAITS);
    builder.attachResourceWrite("gbuffer_depth", egn::DEPTH_STENCIL_ATTACHMENT_TRAITS);
}
static void compileGBufferPass(gbuffer_data &passData, egn::RenderGraphResult const &res, void *pRenderer) {
    EngineRendererImpl &renderer = *static_cast<EngineRendererImpl *>(pRenderer);
    if(!passData.shader.valid()) {
        egn::Entity e = renderer.reg->create();
        e.emplace<egn::vk::Shader>(egn::vk::makeShader({
            .backend = egn::vk::ShaderBackend::SLANG,
            .src = "shaders/deferred/gbuffer.slang",
            .bin = "shaders-bin/deferred/gbuffer.slang",
            .device = renderer.device.device,
            .includeDirs = { "shaders" },
            .definitions = { },
            .debugInfo = true,
        }));
        egn::vk::PipelineLayoutCreateInfo layoutCi{ .unsizedDescriptors = { { 0, 0 } } };
        egn::vk::GraphicsPipelineCreateInfo pipelineCi{
            .dynamicState = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR},
            .input = {
                .bindings = {
                    {0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX},
                    {1, sizeof(egn::Mesh::Geometry::Attributes), VK_VERTEX_INPUT_RATE_VERTEX},
                },
                .attributes = {
                    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, // pos
                    {1, 1, VK_FORMAT_R32G32_SFLOAT,    offsetof(egn::Mesh::Geometry::Attributes, uv)},
                    {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(egn::Mesh::Geometry::Attributes, normal)},
                    {3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(egn::Mesh::Geometry::Attributes, tangent)},
                    {4, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(egn::Mesh::Geometry::Attributes, boneIDs)},
                    {5, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(egn::Mesh::Geometry::Attributes, weights)},
                },
            },
            .attachments = {
                //        albedo                    position                  normal                         pbr
                .color = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM},
                .depth = renderer.depthFormat,
            },
            .depthStencil = {
                .depthTestEnable = true, 
                .depthWriteEnable = true
            },
            .rasterization = {
                .cullMode = VK_CULL_MODE_BACK_BIT,
                .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
            },
            .blending = {
                .attachments = { ALPHA_BLENDING },
            }
        };
        passData.shader = e;
        e.emplace<vk::Pipeline>(vk::makePipeline(*passData.shader, layoutCi, pipelineCi));
        passData.pipeline = e;
        e.name("gbuffer_pipeline");
        vk::allocateDescriptors(passData.pipeline.getc());
    }
    assert(passData.shader.valid() && passData.shader->valid);
    assert(passData.pipeline.valid() && passData.pipeline->valid);

    passData.albedo = res.getResource("gbuffer_albedo");
    passData.position = res.getResource("gbuffer_position");
    passData.normal = res.getResource("gbuffer_normal");
    passData.pbr = res.getResource("gbuffer_pbr");
    passData.depth = res.getResource("gbuffer_depth");
}
static void runGBufferPass(gbuffer_data &passData, VkCommandBuffer cb, void *pRenderer) {
    EngineRendererImpl &renderer = *static_cast<EngineRendererImpl *>(pRenderer);

    auto const &albedo = passData.albedo.get<vk::Image>();
    auto const &position = passData.position.get<vk::Image>();
    auto const &normal = passData.normal.get<vk::Image>();
    auto const &pbr = passData.pbr.get<vk::Image>();
    auto const &depth = passData.depth.get<vk::Image>();

    auto const &pipeline = passData.pipeline.getc();

    VkExtent2D extent = { passData.position.get<vk::Image>().createInfo.image.dimensions.width,
        passData.position.get<vk::Image>().createInfo.image.dimensions.height };

    std::array<VkRenderingAttachmentInfo, 4> colorAttachmentInfos = {
        VkRenderingAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = albedo.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue{ .color{ { 0.0f, 0.0f, 0.2f, 1.0f } } },
        },
        VkRenderingAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = position.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue{ .color{ { 0.0f, 0.0f, 0.0f, 0.0f } } },
        },
        VkRenderingAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = normal.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue{ .color{ { 0.5f, 0.5f, 0.5f, 0.0f } } },
        },
        VkRenderingAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = pbr.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue{ .color{ { 0.0f, 0.0f, 0.0f, 0.0f } } },
        },
    };
    VkRenderingAttachmentInfo depthAttachmentInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depth.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = { .depthStencil = { 1.0f, 0 } },
    };

    VkRenderingInfo renderingInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {
                    .offset = {0, 0},
                    .extent = extent,
                },
                .layerCount = 1,
                .colorAttachmentCount = colorAttachmentInfos.size(),
                .pColorAttachments = colorAttachmentInfos.data(),
                .pDepthAttachment = &depthAttachmentInfo,
            };

    vkCmdBeginRendering(cb, &renderingInfo);

    VkViewport vp{
        .x = 0,
        .y = 0,
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D scissor{
        .offset = { 0, 0 },
        .extent{ .width = extent.width, .height = extent.height },
    };
    vkCmdSetScissor(cb, 0, 1, &scissor);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    egn::vk::bindDescriptorSet(cb, pipeline, 0);

    struct ModelData {
        glm::mat4 modelMat;
        glm::mat4 normMat;
        egn::VulkanMaterial material;
        float _pad0;
    };
    struct UniformBuffer {
        glm::mat4 projMat;
        glm::mat4 viewMat;
    };
    struct PushConstants {
        VkDeviceAddress uniformBuffer;
        VkDeviceAddress instanceData;
    };

    std::map<std::pair<egn::Entity, uint>, std::vector<ModelData>> instanceData;

    UniformBuffer uBufferData{
        .projMat = renderer.camera.get<Camera>().projMat,
        .viewMat = renderer.camera.get<Camera>().viewMat,
    };
    VkDeviceAddress uniformBufferAddress = pushUniform(renderer.uniformBuffer, uBufferData, renderer.frameIndex, 16);

    for(auto eInstance : renderer.reg->view<egn::ModelInstance>()) {
        auto const &instance = eInstance.get<egn::ModelInstance>();
        if(!instance.eModel.valid()) {
            LOG_ERROR("Model instance e has invalid eModel {}", eInstance, instance.eModel);
            continue;
        }
        if(!instance.eModel.contains<egn::VulkanModel>()) {
            LOG_ERROR("Model {} doesent have VulkanModel component!", instance.eModel);
            continue;
        }

        auto &model = instance.eModel.get<egn::VulkanModel>();

        for(uint i = 0; i < model.meshes.size(); ++i) {
            ModelData &modelData = instanceData[{ instance.eModel, i }].emplace_back();
            auto const &mesh = model.meshes[i];

            modelData.modelMat = { 1.0f };
            if(eInstance.contains<egn::Transform>())
                modelData.modelMat = eInstance.get<egn::Transform>().getMat();
            modelData.normMat = glm::transpose(glm::inverse(glm::mat3(modelData.modelMat)));
            modelData.material = mesh.material;
        }
    }

    for(auto const &[eMesh, data] : instanceData) {
        auto const &[eModel, index] = eMesh;
        auto const &meh = eModel.get<egn::VulkanModel>().meshes[index];

        VkDeviceSize vOffset = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &meh.buffers.pos.buffer, &vOffset);
        vkCmdBindVertexBuffers(cb, 1, 1, &meh.buffers.attr.buffer, &vOffset);
        vkCmdBindIndexBuffer(cb, meh.buffers.idx.buffer, 0, VK_INDEX_TYPE_UINT32);

        PushConstants pushConstants;
        pushConstants.uniformBuffer = uniformBufferAddress;
        pushConstants.instanceData = pushUniform(renderer.uniformBuffer, data, renderer.frameIndex, 16);

        vkCmdPushConstants(cb,
            passData.pipeline->layout.layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(pushConstants),
            &pushConstants);

        vkCmdDrawIndexed(cb, meh.indexCount, data.size(), 0, 0, 0);
    }

    vkCmdEndRendering(cb);
}

static void setupLightingPass(egn::RenderPassBuilder &builder, void *) {
    builder->addImageResource("lighting_out", {
        .imageInfo = {
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .view = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
        },
        .resizeToTarget = true,
    });
    builder.attachResourceRead("gbuffer_albedo", SHADER_READ_TRAITS);
    builder.attachResourceRead("gbuffer_position", SHADER_READ_TRAITS);
    builder.attachResourceRead("gbuffer_normal", SHADER_READ_TRAITS);
    builder.attachResourceRead("gbuffer_pbr", SHADER_READ_TRAITS);
    builder.attachResourceWrite("lighting_out", COLOR_ATTACHMENT_TRAITS);
}
static void compileLightingPass(lighting_data &data, egn::RenderGraphResult const &res, void *pRenderer) {
    EngineRendererImpl &renderer = *static_cast<EngineRendererImpl *>(pRenderer);

    if(!data.shader.valid()) {
        auto e = renderer.reg->create();
        e.emplace<egn::vk::Shader>(egn::vk::makeShader({
            .backend = egn::vk::ShaderBackend::SLANG,
            .src = "shaders/deferred/lighting.slang",
            .bin = "shaders-bin/deferred/lighting.slang",
            .device = renderer.device.device,
            .includeDirs = { "shaders" },
            .debugInfo = true,
        }));
        egn::vk::PipelineLayoutCreateInfo layoutCi{
            .dynamicDescriptors = { { .set = 1, .binding = 0 } },
        };
        egn::vk::GraphicsPipelineCreateInfo pipelineCi{
                    .dynamicState = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR },
                    .attachments = {
                        .color = { VK_FORMAT_R16G16B16A16_SFLOAT },
                        .depth = renderer.depthFormat,
                    },
                    .blending = {
                        .attachments = {NO_BLENDING},
                    },
                };
        data.shader = e;
        e.emplace<egn::vk::Pipeline>(egn::vk::makePipeline(data.shader.get<egn::vk::Shader>(), layoutCi, pipelineCi));
        data.pipeline = e;
        e.name("lighting_pipeline");
        egn::vk::allocateDescriptors(data.pipeline.getc());

        renderer.descManager.emplace({
            .dstPipeline = data.pipeline,
            .dstSet = 0,
            .dstBinding = 0,
            .dstFrame = 0,
            .imageInfo = { {
                .resource = res.getResource("gbuffer_albedo"),
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            } },
        });
        renderer.descManager.emplace({
            .dstPipeline = data.pipeline,
            .dstSet = 0,
            .dstBinding = 1,
            .dstFrame = 0,
            .imageInfo = { {
                .resource = res.getResource("gbuffer_position"),
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            } },
        });
        renderer.descManager.emplace({
            .dstPipeline = data.pipeline,
            .dstSet = 0,
            .dstBinding = 2,
            .dstFrame = 0,
            .imageInfo = { {
                .resource = res.getResource("gbuffer_normal"),
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            } },
        });
        renderer.descManager.emplace({
            .dstPipeline = data.pipeline,
            .dstSet = 0,
            .dstBinding = 3,
            .dstFrame = 0,
            .imageInfo = { {
                .resource = res.getResource("gbuffer_pbr"),
                .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            } },
        });
    }
    assert(data.shader.valid() && data.shader.get<egn::vk::Shader>().valid);
    assert(data.pipeline.valid() && data.pipeline.get<egn::vk::Pipeline>().valid);

    data.output = res.getResource("lighting_out");
}
static void runLightingPass(lighting_data &passData, VkCommandBuffer cb, void *pRenderer) {
    EngineRendererImpl &renderer = *static_cast<EngineRendererImpl *>(pRenderer);
    auto const &pipeline = passData.pipeline.getc();

    VkExtent2D extent = { passData.output.get<vk::Image>().createInfo.image.dimensions.width,
        passData.output.get<vk::Image>().createInfo.image.dimensions.height };

    std::array<VkRenderingAttachmentInfo, 1> colorAttachmentInfos = {
        VkRenderingAttachmentInfo{ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = passData.output.get<vk::Image>().view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue{ .color{ { 0.0f, 0.0f, 0.0f, 0.0f } } } },
    };

    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {
            .offset = {0, 0},
            .extent = extent,
        },
        .layerCount = 1,
        .colorAttachmentCount = colorAttachmentInfos.size(),
        .pColorAttachments = colorAttachmentInfos.data(),
        .pDepthAttachment = nullptr,
    };

    vkCmdBeginRendering(cb, &renderingInfo);

    VkViewport vp{
        .x = 0,
        .y = static_cast<float>(extent.height),
        .width = static_cast<float>(extent.width),
        .height = -static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D scissor{
        .offset = { 0, 0 },
        .extent{ .width = extent.width, .height = extent.height },
    };
    vkCmdSetScissor(cb, 0, 1, &scissor);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    egn::vk::bindDescriptorSet(cb, pipeline, 0, 0);

    struct PushConstants {
        // Inverted
        glm::mat4 projMat;
        glm::mat4 viewMat;
    } pushConstants{
        .projMat = glm::inverse(renderer.camera.get<Camera>().projMat),
        .viewMat = glm::inverse(renderer.camera.get<Camera>().viewMat),
    };

    vkCmdPushConstants(cb,
        passData.pipeline->layout.layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(pushConstants),
        &pushConstants);

    vkCmdDraw(cb, 3, 1, 0, 0);

    vkCmdEndRendering(cb);
}

static void setupBlitPass(egn::RenderPassBuilder &builder, void *) {
    builder.attachResourceRead("lighting_out", TRANSFER_SRC_TRAITS);
    builder.attachResourceWrite("target", TRANSFER_DST_TRAITS);
}
static void compileBlitPass(swapchain_blit_data &passData, egn::RenderGraphResult const &res, void *) {
    passData.target = res.getResource("target");
    passData.src = res.getResource("lighting_out");
}
static void runBlitPass(swapchain_blit_data &passData, VkCommandBuffer cb, void *) {
    assert(passData.target);
    auto const &src = passData.src.get<vk::Image>();
    auto const &dst = passData.target.get<egn::vk::Image>();

    VkImageBlit2 region{
        .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
        .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .srcOffsets = { 
            { 0, 0, 0 }, 
            { static_cast<int32_t>(src.createInfo.image.dimensions.width), static_cast<int32_t>(src.createInfo.image.dimensions.height), 1 }, 
        },
        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .dstOffsets = { 
            { 0, 0, 0 }, 
            { static_cast<int32_t>(dst.createInfo.image.dimensions.width), static_cast<int32_t>(dst.createInfo.image.dimensions.height), 1, }, 
        },
    };
    VkBlitImageInfo2 blit{
        .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
        .srcImage = src.image,
        .srcImageLayout = TRANSFER_SRC_TRAITS.imageTraits.layout,
        .dstImage = dst.image,
        .dstImageLayout = TRANSFER_DST_TRAITS.imageTraits.layout,
        .regionCount = 1,
        .pRegions = &region,
        .filter = VK_FILTER_NEAREST,
    };
    vkCmdBlitImage2(cb, &blit);

    // Prepare for presentation
    egn::vk::insertImageMemoryBarrier(cb,
        dst.image,
        TRANSFER_DST_TRAITS.access,
        0,
        TRANSFER_DST_TRAITS.imageTraits.layout,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        TRANSFER_DST_TRAITS.stages,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
}

static void updateResourceAllocator(EngineRendererImpl &renderer) {
    renderer.resourceAllocator.begin();
    for(auto eTexture : renderer.reg->view<egn::Texture2D>(egn::exclude<egn::vk::Image>{ })) {
        renderer.resourceAllocator.processImage(eTexture);
    }
    for(auto eTexture : renderer.reg->view<egn::Model>(egn::exclude<egn::VulkanModel>{ })) {
        renderer.resourceAllocator.processModel(eTexture);
    }
    bool updateImages = renderer.resourceAllocator.end();

    if(updateImages) {
        auto images = renderer.resourceAllocator.getProcessedImages();
        std::vector<egn::ImageDescriptorWrite> imageInfos;
        imageInfos.reserve(images.size());
        for(auto eImage : images) {
            imageInfos.emplace_back(eImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        renderer.descManager.emplace({
            .dstPipeline = static_cast<gbuffer_data const *>(renderer.renderGraph->getRenderGraph().getPass("gbuffer")->storage->data())->pipeline,
            .dstSet = 0,
            .dstBinding = 0,
            .dstFrame = 0,
            .imageInfo = imageInfos,
        });
    }
}

egn::EngineRenderer::EngineRenderer(Registry &reg, vk::Device const &device) {
    mImpl = new EngineRendererImpl();

    mImpl->device = device;
    mImpl->reg = &reg;
    vk::AllocationCreateInfo const ALLOCATION_INFO{
        .device = device.device,
        .physicalDevice = device.physicalDevice,
        .allocator = device.vma,
        .sharingMode = device.queueFamilies.uniqueFamilies.size() == 1 ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
    };

    // clang-format off
    mImpl->uniformBuffer = vk::RingBuffer(reg, {
        .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .allocInfo = ALLOCATION_INFO,
        .size = static_cast<uint32_t>(64 * 1e6), // 64MB
        .map = true,
        .name = "uniform_buffer",
    });
    // clang-format on

    mImpl->commandPool = makeCommandPool(device.queueFamilies.indices.at(VK_QUEUE_GRAPHICS_BIT), device.device);
    mImpl->depthFormat = getDepthFormat(device.physicalDevice);
    mImpl->resourceAllocator = ResourceAllocator(ALLOCATION_INFO, mImpl->commandPool, device.queueFamilies.getQueue(VK_QUEUE_GRAPHICS_BIT));

    egn::RenderGraphBuilder builder;
    builder.setAllocInfo(ALLOCATION_INFO, reg);
    builder.setQueueFamilies(device.queueFamilies);
    builder.resource().addExternalResource("target", { });

    builder.addPass<gbuffer_data>("gbuffer", VK_QUEUE_GRAPHICS_BIT, setupGBufferPass, compileGBufferPass, runGBufferPass);
    builder.addPass<lighting_data>("lighting", VK_QUEUE_GRAPHICS_BIT, setupLightingPass, compileLightingPass, runLightingPass);
    builder.addPass<swapchain_blit_data>("target_blit", VK_QUEUE_TRANSFER_BIT, setupBlitPass, compileBlitPass, runBlitPass);

    setData(mImpl);
    mImpl->renderGraph = this;

    build(std::move(builder));
    assert(initSuccess());
}
egn::EngineRenderer::EngineRenderer(EngineRenderer &&o) {
    operator=(std::move(o));
}
egn::EngineRenderer &egn::EngineRenderer::operator=(EngineRenderer &&o) {
    std::swap(mImpl, o.mImpl);
    mImpl->renderGraph = this;
    setData(mImpl);
    return *this;
}
egn::EngineRenderer::~EngineRenderer() {
    if(mImpl) {
        vkDestroyCommandPool(mImpl->device.device, mImpl->commandPool, nullptr);
        delete mImpl;
    }
}

void egn::EngineRenderer::setCamera(RestrictedEntity<Camera> camera) {
    mImpl->camera = camera;
}

static void resizeRenderGraph(RenderGraphResult const &renderGraph, Entity target) {
    std::unordered_set<Entity> toResize;
    for(auto const &passName : renderGraph.getPassStack()) {
        auto pass = renderGraph.getPass(passName);
        assert(pass);
        for(auto const &info : pass->reads) {
            auto entity = renderGraph.getResource(info.name);
            if(!entity) {
                LOG_WARN("Invalid render graph resource {} when resizing!", entity);
                continue;
            }

            if(entity.contains<ResizeToTarget>()) {
                toResize.emplace(entity);
            }
        }
        for(auto const &info : pass->writes) {
            auto entity = renderGraph.getResource(info.name);
            if(!entity) {
                LOG_WARN("Invalid render graph resource {} when resizing!", entity);
                continue;
            }

            if(entity.contains<ResizeToTarget>()) {
                toResize.emplace(entity);
            }
        }
    }

    auto dimensions = target.get<vk::Image>().createInfo.image.dimensions;
    for(Entity e : toResize) {
        if(e == target) {
            continue;
        }

        if(!e.contains<vk::Image>() && e.contains<vk::ImageCreateInfo>()) {
            auto &ci = e.get<vk::ImageCreateInfo>();
            auto const &ratio = e.get<ResizeToTarget>().ratio;
            ci.image.dimensions = dimensions;
            ci.image.dimensions.width *= ratio.x;
            ci.image.dimensions.height *= ratio.y;
            e.emplace<vk::Image>(vk::makeImage(ci));
            e.erase<vk::ImageCreateInfo>();
            e.try_emplace<ResourceDirty>();
            LOG_TRACE("Making image {} {}x{} with target {}.", e, ci.image.dimensions.width, ci.image.dimensions.height, target);
            continue;
        }

        assert(e.contains<vk::Image>());
        auto &image = e.get<vk::Image>();
        assert(image.valid());

        auto &dstDimensions = image.createInfo.image.dimensions;
        if(dstDimensions == dimensions) {
            continue;
        }

        auto const &ratio = e.get<ResizeToTarget>().ratio;
        vk::destroy(image);
        dstDimensions = dimensions;
        dstDimensions.width *= ratio.x;
        dstDimensions.height *= ratio.y;
        LOG_TRACE("Resizing image {} {}x{} to target {}.", e, dstDimensions.width, dstDimensions.height, target);
        image = vk::makeImage(image.createInfo);
        e.try_emplace<ResourceDirty>();
    }
}
void egn::EngineRenderer::preFrame(RestrictedEntity<vk::Image> target, uint frameIndex) {
    RenderGraphRenderer::preFrame(target, frameIndex);

    if(mImpl->lastTargetDimensions != glm::uvec2(target->createInfo.image.dimensions.width, target->createInfo.image.dimensions.height)) {
        resizeRenderGraph(getRenderGraph(), target);
    }

    updateResourceAllocator(*mImpl);

    if(std::ranges::distance(mImpl->reg->view<egn::ResourceDirty>()))
        LOG_TRACE("{} resources are dirty! {}", std::ranges::distance(mImpl->reg->view<egn::ResourceDirty>()), mImpl->reg->view<egn::ResourceDirty>());

    mImpl->uniformBuffer.free(frameIndex);

    // Only one frame in descriptor sets (currently).
    mImpl->descManager.update(0 /* frameIndex */);

    // Clean up dirty resources that don't have any descriptors.
    for(auto e : mImpl->reg->view<ResourceDirty>(exclude<ResourceUsage>())) {
        e.erase<ResourceDirty>();
    }
}
// A BACKWARDS 360?!? No- NO, HOW IS THIS POSSIBLE!???
void egn::EngineRenderer::render(CommandBuffers &commandBuffers) {
    RenderGraphRenderer::render(commandBuffers);
}
