#include "Scene.hpp"
#include "Controller.hpp"
#include "engine/renderer/ResourceManager.hpp"
#include "engine/scene/Loaders.hpp"
#include "engine/scene/Resources.hpp"

#include <filesystem>
#include <random>

struct CycleAnimations { };

static std::unordered_set<std::string> getSmoothShadedModelPaths() {
    std::ifstream smoothShadeFile("assets/models/smoothShade.txt");
    if(!smoothShadeFile.is_open()) {
        return { };
    }

    std::unordered_set<std::string> res;
    std::string line;
    while(std::getline(smoothShadeFile, line)) {
        if(!line.empty()) {
            res.emplace(std::move(line));
        }
    }

    LOG_TRACE("Smoothly shading {}", res);
    return res;
}
static std::string printTexture(egn::Entity e) {
    if(!e.valid() || !e.contains<egn::Texture2D>())
        return fmt::format("{}", e);
    auto const &texture2D = e.get<egn::Texture2D>();
    return fmt::format("{}, \"{:<30} {}x{}, {:>3} {} mips",
        e,
        texture2D.path + "\",",
        texture2D.bitmap.size.x,
        texture2D.bitmap.size.y,
        (texture2D.srgb ? "srgb" : "not srgb"),
        texture2D.numMipLevels);
}
[[maybe_unused]] [[nodiscard]] static std::string printModelData(egn::Entity e) {
    assert(e.valid() && e.contains<egn::Model>());
    egn::Model const &model = e.get<egn::Model>();
    std::stringstream ss;
    ss << "Model: " << e << ": \"" << model.path << "\"\n";
    ss << "Skeleton: \n";
    ss << "  Bone map size / number of bones: " << model.skeleton.boneMap.size() << "\n";
    if(model.skeleton.boneMap.size() <= 30)
        for(auto const &[name, id] : model.skeleton.boneMap)
            ss << "    [\"" << name << "\": " << id << "]\n";

    ss << "Animations: " << model.animations.size() << "\n";
    for(auto const &animation : model.animations) {
        ss << "-----------------\n";
        ss << "Animation: \"" << animation.name << "\"\n";
        ss << "  Duration: " << animation.durationTicks << " ticks, tps: " << animation.ticksPerSecond << "\n";
        ss << "  Number of bones: " << animation.bones.size() << "\n";
    }

    ss << "Meshes: " << model.meshes.size() << "\n";
    for(auto const &mesh : model.meshes) {
        ss << "-----------------\n";
        ss << "Geometry:\n";
        ss << "  Triangles:   " << mesh.geometry.indices.size() / 3 << "\n";
        ss << "  Indices:     " << mesh.geometry.indices.size() << "\n";
        ss << "  Positions:   " << mesh.geometry.positions.size() << "\n";
        ss << "  Attributes:  " << mesh.geometry.attributes.size() << "\n";
        ss << "Material:\n";
        ss << "  Textures:\n";
        ss << "    Albedo:       " << printTexture(egn::Entity{ &e.reg(), mesh.material.textures.albedo }) << "\n";
        ss << "    Metallic:     " << printTexture(egn::Entity{ &e.reg(), mesh.material.textures.metallic }) << "\n";
        ss << "    Roughness:    " << printTexture(egn::Entity{ &e.reg(), mesh.material.textures.roughness }) << "\n";
        ss << "    Ambient:      " << printTexture(egn::Entity{ &e.reg(), mesh.material.textures.ambient }) << "\n";
        ss << "    Normal:       " << printTexture(egn::Entity{ &e.reg(), mesh.material.textures.normal }) << "\n";
        ss << "    Displacement: " << printTexture(egn::Entity{ &e.reg(), mesh.material.textures.displacement }) << "\n";
        ss << "  Properties:\n";
        ss << "    Ambient:      " << mesh.material.properties.ambient << "\n";
        ss << "    Albedo:       " << mesh.material.properties.albedo << "\n";
        ss << "    Specular:     " << mesh.material.properties.specular << "\n";
        ss << "    Emission:     " << mesh.material.properties.emission << "\n";
        ss << "    Shininess:    " << mesh.material.properties.shininess << "\n";
        ss << "    Metallic:     " << mesh.material.properties.metallic << "\n";
        ss << "    IOR:          " << mesh.material.properties.ior << "\n";
    }

    return ss.str();
}
static egn::Entity loadTexture(egn::Registry &reg, std::string_view path, bool required = true, bool flip = false) {
    static egn::TextureLoader loader(reg.getReg());

    egn::TextureLoaderOptions options{
        .flip = flip,
        .desiredChannels = 4,
    };
    auto eTexture = egn::Entity{ &reg, loader.loadFromFile(path, options) };
    if(!required && !eTexture.valid())
        return eTexture;
    if(required && !eTexture.valid()) {
        LOG_ERROR("Failed to load {}, which is required!", path);
        assert(false);
        return eTexture;
    }

    // ...

    return eTexture;
}
static egn::Entity loadModel(egn::Registry &reg, std::string_view path, std::optional<egn::Material::Textures> textures = { }, bool required = true) {
    static egn::ModelLoader loader(reg.getReg());
    static auto smoothPaths = getSmoothShadedModelPaths();

    egn::ModelLoaderOptions options{
        .flipUVs = false,
        .flipHandiness = true, // ??
        .windingOrder = egn::ModelLoaderOptions::WindingOrder::CCW,
        .smoothNormalAngle = static_cast<float>(smoothPaths.contains(std::string(path)) ? 35 : 0),
    };
    auto eModel = egn::Entity{ &reg, loader.loadFromFile(path, options) };
    if(!required && !eModel.valid())
        return eModel;
    if(required && !eModel.valid()) {
        LOG_ERROR("Failed to load {}, which is required!", path);
        assert(false);
        return eModel;
    }
    auto &model = eModel.get<egn::Model>();

    if(textures.has_value()) {
        auto defaultMaterial = loader.getDefaultMaterial();
        if(textures->albedo == egn::INVALID_ENTITY)
            textures->albedo = defaultMaterial.textures.albedo;
        if(textures->metallic == egn::INVALID_ENTITY)
            textures->metallic = defaultMaterial.textures.metallic;
        if(textures->roughness == egn::INVALID_ENTITY)
            textures->roughness = defaultMaterial.textures.roughness;
        if(textures->ambient == egn::INVALID_ENTITY)
            textures->ambient = defaultMaterial.textures.ambient;
        if(textures->normal == egn::INVALID_ENTITY)
            textures->normal = defaultMaterial.textures.normal;
        if(textures->displacement == egn::INVALID_ENTITY)
            textures->displacement = defaultMaterial.textures.displacement;

        for(auto &mesh : model.meshes)
            mesh.material.textures = textures.value();
    }

    return eModel;
}
static bool
loadModelTo(egn::Registry &reg, std::string_view path, std::vector<egn::Entity> &to, std::optional<egn::Material::Textures> textures = { }) {
    auto e = loadModel(reg, path, textures, false);
    if(e) {
        to.emplace_back(e);
    }

    return e;
}
static void dumpModels(egn::Registry &reg, std::string_view dir) {
    std::ofstream modelStr(std::string(dir.data()) + "/models.txt");
    for(egn::RestrictedEntity<egn::Model> e : reg.view<egn::Model>()) {
        modelStr << printModelData(e) << "Geometry dumps:\n";
        for(uint i = 0; i < e->meshes.size(); ++i) {
            auto const &mesh = e->meshes[i].geometry;
            auto path = dir.data() + ("/" + std::filesystem::path(e->path).stem().string() + "_") + std::to_string(i) + ".obj";
            modelStr << "  " << path << "\n";
            std::ofstream s(path);
            if(!s) {
                continue;
            }

            for(auto const &position : mesh.positions) {
                s << "v " << position.x << " " << position.y << " " << position.z << "\n";
            }
            s << "\n";
            for(auto const &a : mesh.attributes) {
                s << "n " << a.normal.x << " " << a.normal.y << " " << a.normal.z << "\n";
            }
            s << "\n";
            for(auto const &a : mesh.attributes) {
                s << "t " << a.uv.x << " " << a.uv.y << "\n";
            }
            s << "\n";

            for(uint j = 0; j < mesh.indices.size(); j += 3) {
                s << "f " << mesh.indices[j + 0] + 1 << " " << mesh.indices[j + 1] + 1 << " " << mesh.indices[j + 2] + 1 << "\n";
            }
            s << "\n";
        }
    }
}
static egn::Transform lookat(glm::vec3 pos, glm::vec3 center) {
    auto dir = glm::normalize(center - pos);
    auto up = glm::abs(glm::dot(dir, { 0, 1, 0 })) > 0.999 ? glm::vec3{ 1, 0, 0 } : glm::vec3{ 0, 1, 0 };
    return { .position = pos, .orientation = glm::normalize(glm::quatLookAt(dir, up)) };
}
static void assetGrid(egn::Registry &reg, std::vector<egn::Entity> const &props, glm::uvec3 dimensions, glm::vec3 offset, float distance) {
    assert(!props.empty());
    static std::random_device dev;
    static std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> distP(0, props.size() - 1);
    std::uniform_int_distribution<std::mt19937::result_type> distX(0, dimensions.x - 1);
    std::uniform_int_distribution<std::mt19937::result_type> distY(0, dimensions.y - 1);
    std::uniform_int_distribution<std::mt19937::result_type> distZ(0, dimensions.z - 1);
    auto position = [&](float x, float y, float z) -> glm::vec3 { return (glm::vec3(x, y, z) - glm::vec3(dimensions) * 0.5f) * distance + offset; };

    for(uint x = 0; x < dimensions.x; ++x)
        for(uint y = 0; y < dimensions.y; ++y)
            for(uint z = 0; z < dimensions.z; ++z) {
                // x+y*dimensions.x+z*dimensions.x*dimensions.y;
                reg.create(egn::ModelInstance{ props[distP(rng)] }, lookat(position(x, y, z), position(distX(rng), distY(rng), distZ(rng))));
            }
}
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

void scene::TestScene::setupRenderer(egn::Registry &reg, egn::RenderGraphBuilder &builder, egn::DescriptorManager &descManager, egn::vk::Device const &device) {
    egn::ResourceAllocator allocator(ALLOCATION_INFO, commandPool, queueFamilies.getQueue(VK_QUEUE_GRAPHICS_BIT));
    allocator.begin();
    for(auto eTexture : reg.view<egn::Texture2D>(egn::exclude<egn::vk::Image>{ })) {
        allocator.processImage(eTexture);
    }
    for(auto eTexture : reg.view<egn::Model>(egn::exclude<egn::VulkanModel>{ })) {
        allocator.processModel(eTexture);
    }
    allocator.end();

    auto images = allocator.getProcessedImages();
    std::vector<egn::ImageDescriptorWrite> imageInfos;
    imageInfos.reserve(images.size());
    for(auto eImage : images) {
        imageInfos.emplace_back(eImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    [[maybe_unused]] VkPipelineColorBlendAttachmentState constexpr ALPHA_BLENDING{ .blendEnable = true,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = 0xFF };
    [[maybe_unused]] VkPipelineColorBlendAttachmentState constexpr ADDITIVE_BLENDING{
        .blendEnable = true,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = 0xFF,
    };
    [[maybe_unused]] VkPipelineColorBlendAttachmentState constexpr NO_BLENDING{
        .blendEnable = false,
        .colorWriteMask = 0xFF,
    };

    VkFormat const DEPTH_ATTACHMENT_FORMAT = getDepthFormat(initRes.physicalDevice);

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

    struct gbuffer_data {
        egn::RestrictedEntity<egn::vk::Pipeline> pipeline;
        egn::RestrictedEntity<egn::vk::Shader> shader;

        egn::RestrictedEntity<egn::vk::Image> albedo;
        egn::RestrictedEntity<egn::vk::Image> position;
        egn::RestrictedEntity<egn::vk::Image> normal;
        egn::RestrictedEntity<egn::vk::Image> pbr;
        egn::RestrictedEntity<egn::vk::Image> depth;
    };
    builder.addPass<gbuffer_data>(
        "gbuffer",
        VK_QUEUE_GRAPHICS_BIT,
        [&](egn::RenderPassBuilder &builder) {
            auto extent = swapchain->createInfo.imageExtent;
            builder->addImageResource("gbuffer_albedo", {
                .imageInfo = {
                    .format = VK_FORMAT_R8G8B8A8_UNORM,
                    .dimensions = {extent.width, extent.height},
                    .view = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
                },
                .resizeToSwapchain = true,
            });
            builder->addImageResource("gbuffer_position", {
                .imageInfo = {
                    .format = VK_FORMAT_R8G8B8A8_UNORM,
                    .dimensions = {extent.width, extent.height},
                    .view = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
                },
                .resizeToSwapchain = true,
            });
            builder->addImageResource("gbuffer_normal", {
                .imageInfo = {
                    .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                    .dimensions = {extent.width, extent.height},
                    .view = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
                },
                .resizeToSwapchain = true,
            });
            builder->addImageResource("gbuffer_pbr", {
                .imageInfo = {
                    .format = VK_FORMAT_R8G8B8A8_UNORM,
                    .dimensions = {extent.width, extent.height},
                    .view = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
                },
                .resizeToSwapchain = true,
            });
            builder->addImageResource("gbuffer_depth", { 
                .imageInfo = { 
                    .format = DEPTH_ATTACHMENT_FORMAT,
                    .dimensions = { extent.width, extent.height },
                    .view = { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT }, 
                },
                .resizeToSwapchain = true, 
            });

            builder.attachResourceWrite("gbuffer_albedo", COLOR_ATTACHMENT_TRAITS);
            builder.attachResourceWrite("gbuffer_position", COLOR_ATTACHMENT_TRAITS);
            builder.attachResourceWrite("gbuffer_normal", COLOR_ATTACHMENT_TRAITS);
            builder.attachResourceWrite("gbuffer_pbr", COLOR_ATTACHMENT_TRAITS);
            builder.attachResourceWrite("gbuffer_depth", DEPTH_STENCIL_ATTACHMENT_TRAITS);
        },
        [&](gbuffer_data &data, egn::RenderGraphResult const &res) {
            if(!data.shader.valid()) {
                egn::Entity e = reg.create();
                e.emplace<egn::vk::Shader>(egn::vk::makeShader({
                    .backend = egn::vk::ShaderBackend::SLANG,
                    .src = "shaders/deferred/gbuffer.slang",
                    .bin = "shaders-bin/deferred/gbuffer.slang",
                    .device = device,
                    .includeDirs = { "shaders" },
                    .definitions = { },
                    .debugInfo = true,
                }));
                egn::vk::PipelineLayoutCreateInfo layoutCi{
                    .unsizedDescriptorSize = { { { .set = 0, .binding = 0 }, images.size() } },
                };
                egn::vk::GraphicsPipelineCreateInfo pipelineCi{
                    .dynamicState = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR},
                    .input = {
                        .bindings = {
                            {0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX},
                            {1, sizeof(egn::Mesh::Geometry::Attributes), VK_VERTEX_INPUT_RATE_VERTEX},
                        },
                        .attributes = {
                            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, // pos
                            {1, 1, VK_FORMAT_R32G32_SFLOAT,    offsetof(egn::Mesh::Geometry::Attributes, uv)}, // uv
                            {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(egn::Mesh::Geometry::Attributes, normal)}, // norm
                            {3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(egn::Mesh::Geometry::Attributes, tangent)}, // tan
                            {4, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(egn::Mesh::Geometry::Attributes, boneIDs)}, // tan
                            {5, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(egn::Mesh::Geometry::Attributes, weights)}, // tan
                        },
                    },
                    .attachments = {
                        //        albedo                    position                  normal                         pbr
                        .color = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM},
                        .depth = DEPTH_ATTACHMENT_FORMAT,
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
                data.shader = e;
                e.emplace<egn::vk::Pipeline>(egn::vk::makePipeline(*data.shader, layoutCi, pipelineCi));
                data.pipeline = e;
                e.name("gbuffer_pipeline");
                egn::vk::allocateDescriptors(data.pipeline.getc());
                descManager.addResource({
                    .dstPipeline = data.pipeline,
                    .dstSet = 0,
                    .dstBinding = 0,
                    .dstFrame = 0,
                    .imageInfo = imageInfos,
                });
            }
            assert(data.shader.valid() && data.shader->valid);
            assert(data.pipeline.valid() && data.pipeline->valid);

            data.albedo = res.getResource("gbuffer_albedo");
            data.position = res.getResource("gbuffer_position");
            data.normal = res.getResource("gbuffer_normal");
            data.pbr = res.getResource("gbuffer_pbr");
            data.depth = res.getResource("gbuffer_depth");
        },
        [&](gbuffer_data &passData, VkCommandBuffer cb) {
            auto const &albedo = passData.albedo.getc();
            auto const &position = passData.position.getc();
            auto const &normal = passData.normal.getc();
            auto const &pbr = passData.pbr.getc();
            auto const &depth = passData.depth.getc();

            auto const &pipeline = passData.pipeline.getc();

            VkExtent2D extent = swapchain->createInfo.imageExtent;

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
                .extent{ .width = window->size.x, .height = window->size.y },
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
                .projMat = eCamera->projMat,
                .viewMat = eCamera->viewMat,
            };
            VkDeviceAddress uniformBufferAddress = pushUniform(uniformBuffer, uBufferData, frameIndex, 16);

            for(auto eInstance : reg.view<egn::ModelInstance>()) {
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
                pushConstants.instanceData = pushUniform(uniformBuffer, data, frameIndex, 16);

                VkPushConstantsInfo pushConstantsInfo{
                    .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                    .layout = passData.pipeline->layout.layout,
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    .offset = 0,
                    .size = sizeof(pushConstants),
                    .pValues = &pushConstants,
                };
                vkCmdPushConstants2(cb, &pushConstantsInfo);

                vkCmdDrawIndexed(cb, meh.indexCount, data.size(), 0, 0, 0);
            }

            vkCmdEndRendering(cb);
        }
    );
    struct lighting_data {
        egn::RestrictedEntity<egn::vk::Pipeline> pipeline;
        egn::RestrictedEntity<egn::vk::Shader> shader;

        egn::RestrictedEntity<egn::vk::Image> output;
    };
    builder.addPass<lighting_data>(
        "lighting",
        VK_QUEUE_GRAPHICS_BIT,
        [&](egn::RenderPassBuilder &builder) {
            auto extent = swapchain->createInfo.imageExtent;
            builder->addImageResource("lighting_out", {
                .imageInfo = {
                    .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                    .dimensions = {extent.width, extent.height},
                    .view = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
                },
                .resizeToSwapchain = true,
            });
            builder.attachResourceRead("gbuffer_albedo", SHADER_READ_TRAITS);
            builder.attachResourceRead("gbuffer_position", SHADER_READ_TRAITS);
            builder.attachResourceRead("gbuffer_normal", SHADER_READ_TRAITS);
            builder.attachResourceRead("gbuffer_pbr", SHADER_READ_TRAITS);
            builder.attachResourceWrite("lighting_out", COLOR_ATTACHMENT_TRAITS);
        },
        [&](lighting_data &data, egn::RenderGraphResult const &res) {
            if(!data.shader.valid()) {
                auto e = reg.create();
                e.emplace<egn::vk::Shader>(egn::vk::makeShader({
                    .backend = egn::vk::ShaderBackend::SLANG,
                    .src = "shaders/deferred/lighting.slang",
                    .bin = "shaders-bin/deferred/lighting.slang",
                    .device = device,
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
                        .depth = DEPTH_ATTACHMENT_FORMAT,
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
                descManager.addResource({
                    .dstPipeline = data.pipeline,
                    .dstSet = 0,
                    .dstBinding = 0,
                    .dstFrame = 0,
                    .imageInfo = { {
                        .resource = res.getResource("gbuffer_albedo"),
                        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    } },
                });
                descManager.addResource({
                    .dstPipeline = data.pipeline,
                    .dstSet = 0,
                    .dstBinding = 1,
                    .dstFrame = 0,
                    .imageInfo = { {
                        .resource = res.getResource("gbuffer_position"),
                        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    } },
                });
                descManager.addResource({
                    .dstPipeline = data.pipeline,
                    .dstSet = 0,
                    .dstBinding = 2,
                    .dstFrame = 0,
                    .imageInfo = { {
                        .resource = res.getResource("gbuffer_normal"),
                        .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    } },
                });
                descManager.addResource({
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
        },
        [&](lighting_data &data, VkCommandBuffer cb) {
            auto const &pipeline = data.pipeline.getc();

            VkExtent2D extent = swapchain->createInfo.imageExtent;

            std::array<VkRenderingAttachmentInfo, 1> colorAttachmentInfos = {
                VkRenderingAttachmentInfo{ .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = data.output->view,
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
                .extent{ .width = window->size.x, .height = window->size.y },
            };
            vkCmdSetScissor(cb, 0, 1, &scissor);

            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
            egn::vk::bindDescriptorSet(cb, pipeline, 0, 0);

            struct PushConstants {
                // Inverted
                glm::mat4 projMat;
                glm::mat4 viewMat;
            } pushConstants{
                .projMat = glm::inverse(eCamera->projMat),
                .viewMat = glm::inverse(eCamera->viewMat),
            };

            VkPushConstantsInfo pushConstantsInfo{
                .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
                .layout = data.pipeline->layout.layout,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .offset = 0,
                .size = sizeof(pushConstants),
                .pValues = &pushConstants,
            };
            vkCmdPushConstants2(cb, &pushConstantsInfo);

            vkCmdDraw(cb, 3, 1, 0, 0);

            vkCmdEndRendering(cb);
        }
    );

    struct swapchain_blit_data {
        egn::RestrictedEntity<egn::vk::Image> lighting_out;
        egn::Entity target;
    };
    builder.addPass<swapchain_blit_data>(
        "target_blit",
        VK_QUEUE_TRANSFER_BIT,
        [&](egn::RenderPassBuilder &builder) {
            builder.attachResourceRead("lighting_out", TRANSFER_SRC_TRAITS);
            builder.attachResourceWrite("target", TRANSFER_DST_TRAITS);
        },
        [&](swapchain_blit_data &data, egn::RenderGraphResult const &res) {
            data.target = res.getResource("target");
            data.lighting_out = res.getResource("lighting_out");
        },
        [&](swapchain_blit_data &data, VkCommandBuffer cb) {
            assert(data.target);
            auto const &src = data.lighting_out.getc();
            auto const &dst = data.target.get<egn::vk::Image>();

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
    );
}
void scene::TestScene::setup(egn::Registry &reg) {
    const auto prototypeTextures = egn::Material::Textures{ .albedo = loadTexture(reg, "assets/textures/prototype/texture_03.png", false) };
    std::vector<egn::Entity> props;
    loadModelTo(reg, "assets/models/suzanne.glb", props, prototypeTextures);
    loadModelTo(reg, "assets/models/cube.glb", props, prototypeTextures);
    loadModelTo(reg, "assets/models/sphere.glb", props, prototypeTextures);
    loadModelTo(reg, "assets/models/teapot.glb", props, prototypeTextures);

    const glm::uvec3 numProps = { 10, 3, 5 };
    const float distance = 2;
    const glm::vec3 offset = { 0, -1, -5 };
    assetGrid(reg, props, numProps, offset, distance);

    const glm::vec3 startPos(0, -4, 0);
    const glm::vec3 step(2, 0, 0);
    for(uint i = 0; i < props.size(); ++i) {
        reg.create(egn::ModelInstance{ props[i] }, egn::Transform{ .position = startPos + static_cast<float>(i) * step });
    }

    const auto cubes = loadModel(reg, "assets/models/deccer_cubes/SM_Deccer_Cubes_Textured_Complex.gltf", { });
    if(cubes) {
        reg.create(egn::ModelInstance{ cubes }, egn::Transform{ .position = { -10, 0, 10 } });
    }

    auto fox = loadModel(reg, "assets/models/_fox.glb");
    fox.emplace<CycleAnimations>();
    fox.emplace<egn::ModelInstance>(fox);

    dumpModels(reg, "tmp/models");

    mEventListener = reg.create<egn::WindowEventListener>();
    mCamera = Controller::createCamera(window, { 0, 2, 4 }, { 0, 0, 0 });
}
void scene::TestScene::update(egn::Registry &reg, float deltatime) {
    auto &listener = eListener.get<egn::WindowEventListener>();
    while(!listener.keyEvents.empty()) {
        auto event = listener.keyEvents.front();
        listener.keyEvents.pop();

        if(event.key == GLFW_KEY_R && event.action == GLFW_PRESS) {
            CHECK_VK_RES(vkDeviceWaitIdle(device));
            for(auto e : reg.view<egn::vk::Pipeline, egn::vk::Shader>()) {
                auto &pipeline = e.get<egn::vk::Pipeline>();
                auto &shader = e.get<egn::vk::Shader>();

                auto newShader = egn::vk::makeShader(shader.createInfo);
                if(!newShader.valid)
                    continue;

                egn::vk::destroy(shader);
                shader = newShader;
                egn::vk::destroy(pipeline);
                switch(pipeline.type) {
                    case egn::vk::Pipeline::Type::Graphics:
                        pipeline = egn::vk::makePipeline(shader, pipeline.createInfo.layout, pipeline.createInfo.graphics);
                        break;

                    case egn::vk::Pipeline::Type::Compute:
                        pipeline = egn::vk::makePipeline(shader, pipeline.createInfo.layout, pipeline.createInfo.compute);
                        break;

                    case egn::vk::Pipeline::Type::RayTracing:
                        pipeline = egn::vk::makePipeline(shader, pipeline.createInfo.layout, pipeline.createInfo.raytracing);
                        break;

                    default: assert(false && "unknown pipeline");
                }
                egn::vk::allocateDescriptors(pipeline);

                e.emplace<egn::ResourceDirty>();
            }
        }
    }

    cameraController.update(reg, deltatime);
}
