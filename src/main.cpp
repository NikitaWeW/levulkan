#include <bits/stdc++.h>

#include "vk/vk.hpp"
#include "ECS.hpp"
#include "cpptrace/from_current.hpp"

#include "Logging.hpp"
#include "IO.hpp"
#include "Renderdoc.hpp"
#include "Controller.hpp"   
#include "resource/Resources.hpp"
#include "Renderer.hpp"

Registry sReg;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

////////////////////////////////////////////////////////////////

/* 
struct ShaderCompilerCreateInfo {
    VkDevice device = VK_NULL_HANDLE;

    fs::IFilesystem *fs = nullptr;

    std::string srcPrefix = "shaders";
    std::string binPrefix = "shaders-bin";
    std::vector<std::string> includeDirs;
    std::vector<std::string> systemIncludeDirs;
    std::vector<std::pair<std::string, std::string>> definitions;

    uint32_t targetVersion = VK_API_VERSION_1_3;
    vk::SpirvVersion spirvVersion = vk::SpirvVersion::SpirvVersion_1_6;
};
*/

struct MatrixData {
    struct CameraData {
        glm::mat4 projMat;
        glm::mat4 viewMat;
    } camera;
    struct ModelData {
        glm::mat4 modelMat;
        glm::mat4 normMat;
    } model;
};

struct UniformBuffer {
    VulkanMaterial uMaterial;
    MatrixData uMatrixData;
};

template<typename T>
static T hash_combine(T lhs, T rhs) {
    lhs ^= rhs + T(0x9e3779b9) + (lhs << T(6)) + (lhs >> T(2));
    return lhs;
}

static std::string printTexture(Entity e) {
    if(!e.valid() || !e.has<Texture2D>())
        return fmt::format("{}", e);
    auto const &texture2D = e.get<Texture2D>();
    return fmt::format("{}, \"{:<30} {}x{}, {:>3} {} mips", e, texture2D.path + "\",", texture2D.bitmap.size.x, texture2D.bitmap.size.y,(texture2D.srgb ? "srgb" : "not srgb"), texture2D.numMipLevels);
}
[[maybe_unused]] static void printModelData(Entity e) {
    assert(e.valid() && e.has<Model>());
    Model const &model = e.get<Model>();
    LOG_INFO("");
    LOG_INFO("Model: {}: \"{}\"", e, model.path);
    LOG_INFO("Skeleton: ");
    LOG_INFO("  Bone map size / number of bones: {}", model.skeleton.boneMap.size());
    if(model.skeleton.boneMap.size() <= 30)
        for(auto const &[name, id] : model.skeleton.boneMap)
            LOG_INFO("    [\"{}\": {}]", name, id);

    LOG_INFO("Animations: {}", model.animations.size());
    for(auto const &animation : model.animations)
    {
        LOG_INFO("-----------------");
        LOG_INFO("Animation: \"{}\"", animation.name);
        LOG_INFO("  Duration: {} ticks, tps: {}", animation.durationTicks, animation.ticksPerSecond);
        LOG_INFO("  Bones size: {}", animation.bones.size());
    }

    LOG_INFO("Meshes: {}", model.meshes.size());
    for(auto const &mesh : model.meshes)
    {
        LOG_INFO("-----------------");

        LOG_INFO("Geometry:");
        LOG_INFO("  Triangles: {}", mesh.geometry.indices.size() / 3);
        LOG_INFO("  Indices:   {}", mesh.geometry.indices.size());
        LOG_INFO("  Positions: {}", mesh.geometry.positions.size());
        LOG_INFO("  TexCoords: {}", mesh.geometry.texCoords.size());
        LOG_INFO("  Normals:   {}", mesh.geometry.normals.size());
        LOG_INFO("  Tangents:  {}", mesh.geometry.tangents.size());
        LOG_INFO("  BoneIDs:   {}", mesh.geometry.boneIDs.size());
        LOG_INFO("  Weights:   {}", mesh.geometry.weights.size());
        
        LOG_INFO("Material:");
        LOG_INFO("Textures:");
        LOG_INFO("  Albedo:       {}", printTexture(Entity{&e.reg(), mesh.material.textures.albedo}));
        LOG_INFO("  Metallic:     {}", printTexture(Entity{&e.reg(), mesh.material.textures.metallic}));
        LOG_INFO("  Roughness:    {}", printTexture(Entity{&e.reg(), mesh.material.textures.roughness}));
        LOG_INFO("  Ambient:      {}", printTexture(Entity{&e.reg(), mesh.material.textures.ambient}));
        LOG_INFO("  Normal:       {}", printTexture(Entity{&e.reg(), mesh.material.textures.normal}));
        LOG_INFO("  Displacement: {}", printTexture(Entity{&e.reg(), mesh.material.textures.displacement}));
        LOG_INFO("Properties:");
        LOG_INFO("  Ambient:       {}", fmt::streamed(mesh.material.properties.ambient));
        LOG_INFO("  Albedo:        {}", fmt::streamed(mesh.material.properties.albedo));
        LOG_INFO("  Specular:      {}", fmt::streamed(mesh.material.properties.specular));
        LOG_INFO("  Emission:      {}", fmt::streamed(mesh.material.properties.emission));
        LOG_INFO("  Shininess:     {}", mesh.material.properties.shininess);
        LOG_INFO("  Metallic:      {}", mesh.material.properties.metallic);
        LOG_INFO("  IOR:           {}", mesh.material.properties.ior);
    }
}
static Entity loadTexture(std::string_view path, TextureLoaderOptions options = {}, bool required = true)
{
    static TextureLoader loader(sReg.getReg());
    
    auto eTexture = Entity{&sReg, loader.loadFromFile(path, options)};
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
static Entity loadModel(std::string_view path, std::optional<Material::Textures> textures = {}, ModelLoaderOptions options = {}, bool required = true) {
    static ModelLoader loader(sReg.getReg());
    
    auto eModel = Entity{&sReg, loader.loadFromFile(path, options)};
    if(!required && !eModel.valid())
        return eModel;
    if(required && !eModel.valid()) {
        LOG_ERROR("Failed to load {}, which is required!", path);
        assert(false);
        return eModel;
    }
    auto &model = eModel.get<Model>();
    
    if(textures.has_value())
    {
        auto defaultMaterial = loader.getDefaultMaterial();
        if(textures->albedo       == INVALID_ENTITY) textures->albedo       = defaultMaterial.textures.albedo;
        if(textures->metallic     == INVALID_ENTITY) textures->metallic     = defaultMaterial.textures.metallic;
        if(textures->roughness    == INVALID_ENTITY) textures->roughness    = defaultMaterial.textures.roughness;
        if(textures->ambient      == INVALID_ENTITY) textures->ambient      = defaultMaterial.textures.ambient;
        if(textures->normal       == INVALID_ENTITY) textures->normal       = defaultMaterial.textures.normal;
        if(textures->displacement == INVALID_ENTITY) textures->displacement = defaultMaterial.textures.displacement;

        for(auto &mesh : model.meshes)
            mesh.material.textures = textures.value();
    }

    return eModel;
}
static Transform lookat(glm::vec3 pos, glm::vec3 center) {
    auto dir = glm::normalize(center - pos);
    auto up = glm::abs(glm::dot(dir, {0,1,0})) > 0.999 ? glm::vec3{1,0,0} : glm::vec3{0,1,0};
    return {
        .position = pos,
        .orientation = glm::normalize(glm::quatLookAt(dir, up))
    };
}
static Entity makeWindow(Registry &reg, std::string_view name) {
    auto eWindow = reg.create(Window{}, Name("window_" + std::string(name)));
    auto &window = eWindow.get<Window>();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window.handle = glfwCreateWindow(800, 600, name.data(), nullptr, nullptr);
    glfwGetWindowSize(window.handle, reinterpret_cast<int *>(&window.size.x), reinterpret_cast<int *>(&window.size.y));
    glfwSwapInterval(1);
    if(glfwRawMouseMotionSupported())
        glfwSetInputMode(window.handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    io::setCallbacks(window.handle, reg.getReg());

    return eWindow;
}
static VkCommandPool createCommandPool(uint32_t index, VkDevice device) {
    VkCommandPoolCreateInfo commandPoolCI{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = index
    };
    VkCommandPool commandPool;
    vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool);
    return commandPool;
}
[[maybe_unused]] static VkFence createFence(VkDevice dev) {
    VkFence fence = nullptr;
    VkFenceCreateInfo fenceCI{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    CHECK_VK_RES(vkCreateFence(dev, &fenceCI, nullptr, &fence));
    return fence;
}
static void assetGrid(std::vector<Entity> const &props, glm::uvec3 dimensions, glm::vec3 offset, float distance) {
    static std::random_device dev;
    static std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> distP(0, props.size()-1);
    std::uniform_int_distribution<std::mt19937::result_type> distX(0, dimensions.x-1);
    std::uniform_int_distribution<std::mt19937::result_type> distY(0, dimensions.y-1);
    std::uniform_int_distribution<std::mt19937::result_type> distZ(0, dimensions.z-1);
    auto position = [&](float x, float y, float z) -> glm::vec3 {
        return (glm::vec3(x, y, z) - glm::vec3(dimensions) * 0.5f ) * distance + offset;
    };

    for(uint x = 0; x < dimensions.x; ++x)
        for(uint y = 0; y < dimensions.y; ++y)
            for(uint z = 0; z < dimensions.z; ++z) {
        // x+y*dimensions.x+z*dimensions.x*dimensions.y;
        sReg.create(ModelInstance{props[distP(rng)]}, lookat(
            position(x, y, z), 
            position(distX(rng), distY(rng), distZ(rng))
        ));
    }
}
static VkFormat getDepthFormat(VkPhysicalDevice dev) {
    VkFormat outFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkFormat> depthFormatList{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM };
    for(VkFormat &format : depthFormatList) {
        VkFormatProperties2 formatProperties{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
        vkGetPhysicalDeviceFormatProperties2(dev, format, &formatProperties);
        if(formatProperties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            outFormat = format;
            break;
        }
    }
    if(outFormat == VK_FORMAT_UNDEFINED)
    {
        LOG_ERROR("Failed to pick depth image format!");
        outFormat = depthFormatList.at(0);
    }

    return outFormat;
}

////////////////////////////////////////////////////////////////

static bool init() {
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

    VkResult res = volkInitialize();
    if(res != VK_SUCCESS)
    {
        LOG_ERROR("Failed to init volk: {}!", string_VkResult(res));
        return false;
    }

    return true;
}
int app([[maybe_unused]] int argc, [[maybe_unused]] char **argv) {
    if(!init())
    {
        LOG_ERROR("Failed to init!");
        return -1;
    }

    DirectEntity<Window> window = makeWindow(sReg, "levulkan");

    vk::InitInfo initInfo{
        .appName = "levulkan",
        .window = window->handle,
        .version = VK_API_VERSION_1_4,
        .queues = {VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_TRANSFER_BIT},
        .deviceFeatures = {
            .features = {
                .geometryShader = true,
                .shaderSampledImageArrayDynamicIndexing = true,
            },
            .vulkan12 = {
                .descriptorIndexing = true,
                .descriptorBindingVariableDescriptorCount = true,
                .runtimeDescriptorArray = true,
                .bufferDeviceAddress = true,
            },
            .vulkan13 = {
                .synchronization2 = true,
                .dynamicRendering = true,
            },
        },
    };

    vk::enableValidationLayers(initInfo);

    [[maybe_unused]] static class VulkanInitializer
    {
    private:
        vk::InitResult initRes;
    public:
        inline VulkanInitializer(vk::InitInfo const &initInfo) { initRes = vk::init(initInfo); }
        VulkanInitializer(VulkanInitializer &&) = default;
        VulkanInitializer &operator=(VulkanInitializer &&) = default;
        VulkanInitializer(VulkanInitializer const &) = delete;
        VulkanInitializer &operator=(VulkanInitializer const &) = delete;
        inline ~VulkanInitializer() { 
            vmaDestroyAllocator(initRes.vma);
            vkDestroyDevice(initRes.device, nullptr);
            vkDestroySurfaceKHR(initRes.instance, initRes.surface, nullptr);
            vkDestroyDebugUtilsMessengerEXT(initRes.instance, initRes.debugMessenger, nullptr);
            vkDestroyInstance(initRes.instance, nullptr);
        }

        inline vk::InitResult const &getInitRes() const { return initRes; }
        inline vk::InitResult &getInitRes() { return initRes; }
    } vulkanInitializer(initInfo);
    vk::InitResult const &initRes = vulkanInitializer.getInitRes();
    if(!initRes.success)
    {
        LOG_ERROR("Failed to init vulkan!");
        return -1;
    }
    auto &device = initRes.device;

    VkSharingMode const SHARING_MODE = initRes.queueFamilies.uniqueFamilies.size() == 1 ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
    LOG_TRACE("SHARING_MODE={}", string_VkSharingMode(SHARING_MODE));

    DirectEntity<vk::Swapchain> swapchain = sReg.create(vk::makeSwapchain({
        .alloc = {
            .device = device,
            .physicalDevice = initRes.physicalDevice,
            .surface = initRes.surface,
        },
        .size = {window->size.x, window->size.y},
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .registry = &sReg
    }), Name("swapchain_" + (window.has<Name>() ? window.get<Name>().name : "")));

    VkCommandPool commandPool = createCommandPool(initRes.queueFamilies.indices.at(VK_QUEUE_GRAPHICS_BIT), device);
    VkQueue graphicsQueue = initRes.queueFamilies.getQueue(VK_QUEUE_GRAPHICS_BIT);

    vk::AllocationCreateInfo const ALLOCATION_INFO{
        .device = initRes.device,
        .allocator = initRes.vma,
        .sharingMode = SHARING_MODE
    };

    ////////////////////////////////////////////////////////////////

    { // Scene
        const auto prototypeTextures = Material::Textures{
            .albedo = loadTexture("assets/textures/prototype/texture_03.png", {}, false)
        };
        const auto suzanne = loadModel("assets/models/suzanne.glb", prototypeTextures);
        const auto cube    = loadModel("assets/models/cube.glb",    prototypeTextures);
        const auto sphere  = loadModel("assets/models/sphere.glb",  prototypeTextures);
        const auto teapot  = loadModel("assets/models/teapot.glb",  prototypeTextures);
        
        const std::vector<Entity> props{suzanne, cube, sphere, teapot};
        const glm::uvec3 numProps = {10, 3, 5};
        const float distance = 2;
        const glm::vec3 offset = {0, -1, -5};
        assetGrid(props, numProps, offset, distance);

        const auto cubes = loadModel("assets/models/deccer_cubes/SM_Deccer_Cubes_Textured_Complex.gltf");
        sReg.create(ModelInstance{cube}, Transform{.position = {0, -4, 0}}); 
        sReg.create(ModelInstance{cubes}, Transform{.position = {-10, 0, 10}}); 
    }
    
    ////////////////////////////////////////////////////////////////

    ResourceAllocator allocator(ALLOCATION_INFO, commandPool, graphicsQueue);
    allocator.begin();
    for(auto eTexture : sReg.view<Texture2D>(exclude<vk::Image>{})) {
        allocator.processImage(eTexture);
    }
    for(auto eTexture : sReg.view<Model>(exclude<VulkanModel>{})) {
        allocator.processModel(eTexture);
    }
    allocator.end();

    auto images = allocator.getProcessedImages();

    DescriptorManager descManager;

    vk::RingBuffer uniformBuffer(sReg, {
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .allocInfo = ALLOCATION_INFO,
        .size = static_cast<uint32_t>(64*1e6), // 64MB
        .map = true
    });

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(initRes.physicalDevice, &properties);

    ////////////////////////////////////////////////////////////////

    uint frameIndex = 0;
    uint imageIndex = 0;
    float deltatime = 1e-6;

    auto eCamera = Controller::createCamera(sReg, {0, 2, 4}, {0, 0, 0});
    Controller::Camera &camera = eCamera.get<Controller::Camera>();


    [[maybe_unused]] VkPipelineColorBlendAttachmentState constexpr ALPHA_BLENDING{
        .blendEnable = true,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = 0xFF
    };
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

    ResourceTraits constexpr COLOR_ATTACHMENT_TRAITS{
        .imageTraits = {
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        },
        .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .stages = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
    };
    ResourceTraits constexpr DEPTH_STENCIL_ATTACHMENT_TRAITS{
        .imageTraits = {
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
            .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        },
        .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .stages = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
    };
    ResourceTraits constexpr SHADER_READ_TRAITS{
        .imageTraits = {
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
        .access = VK_ACCESS_2_SHADER_READ_BIT,
        .stages = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
    };
    ResourceTraits constexpr TRANSFER_SRC_TRAITS{
        .imageTraits = {
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        },
        .access = VK_ACCESS_2_TRANSFER_READ_BIT,
        .stages = VK_PIPELINE_STAGE_2_TRANSFER_BIT
    };
    ResourceTraits constexpr TRANSFER_DST_TRAITS{
        .imageTraits = {
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        },
        .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .stages = VK_PIPELINE_STAGE_2_TRANSFER_BIT
    };

    struct gbuffer_data {
        DirectEntity<vk::Pipeline> pipeline;
        DirectEntity<vk::Shader> shader;

        DirectEntity<vk::Image> albedo;
        DirectEntity<vk::Image> position;
        DirectEntity<vk::Image> normal;
        DirectEntity<vk::Image> pbr;
        DirectEntity<vk::Image> depth;
    };
    RenderGraphBuilder builder;
    builder.setAllocInfo(ALLOCATION_INFO, sReg);
    builder.setQueueFamilies(initRes.queueFamilies);
    builder.addPass<gbuffer_data>("gbuffer", VK_QUEUE_GRAPHICS_BIT, [&](RenderPassBuilder &builder){
        auto extent = swapchain.get<vk::Swapchain>().createInfo.imageExtent;
        builder.addImageResource("gbuffer_albedo", {.imageInfo = {
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .dimensions = {extent.width, extent.height},
            .view = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT }
        }, .resizeToSwapchain = true });
        builder.addImageResource("gbuffer_position", {.imageInfo = {
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .dimensions = {extent.width, extent.height},
            .view = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT }
        }, .resizeToSwapchain = true });
        builder.addImageResource("gbuffer_normal", {.imageInfo = {
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .dimensions = {extent.width, extent.height},
            .view = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT }
        }, .resizeToSwapchain = true });
        builder.addImageResource("gbuffer_pbr", {.imageInfo = {
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .dimensions = {extent.width, extent.height},
            .view = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT }
        }, .resizeToSwapchain = true });
        builder.addImageResource("gbuffer_depth", {.imageInfo = {
            .format = DEPTH_ATTACHMENT_FORMAT,
            .dimensions = {extent.width, extent.height},
            .view = { .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT }
        }, .resizeToSwapchain = true });

        builder.attachResourceWrite("gbuffer_albedo",  COLOR_ATTACHMENT_TRAITS);
        builder.attachResourceWrite("gbuffer_position",COLOR_ATTACHMENT_TRAITS);
        builder.attachResourceWrite("gbuffer_normal",  COLOR_ATTACHMENT_TRAITS);
        builder.attachResourceWrite("gbuffer_pbr",     COLOR_ATTACHMENT_TRAITS);
        builder.attachResourceWrite("gbuffer_depth",   DEPTH_STENCIL_ATTACHMENT_TRAITS);
    }, [&](gbuffer_data &data, RenderGraphResult const &res){
        if(!data.shader.valid()) {
            Entity e = sReg.create();
            e.emplace<vk::Shader>(vk::makeShader({
                .backend = vk::ShaderBackend::SLANG,
                .src = "shaders/deferred/gbuffer.slang",
                .bin = "shaders-bin/deferred/gbuffer.slang",
                .device = device,
                .includeDirs = {"shaders"},
                .debugInfo = true
            }));
            vk::PipelineLayoutCreateInfo layoutCi{
                .dynamicDescriptors = {{.set = 0, .binding = 0}},
                .unsizedDescriptorSize = {
                    {{.set = 1, .binding = 0}, images.size()}
                },
            };
            vk::GraphicsPipelineCreateInfo pipelineCi{
                .dynamicState = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR },
                .input = {
                    .bindings = {
                        { 0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX },
                        { 1, sizeof(glm::vec2), VK_VERTEX_INPUT_RATE_VERTEX },
                        { 2, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX },
                        { 3, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX },
                    },
                    .attributes = {
                        { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }, // pos
                        { 1, 1, VK_FORMAT_R32G32_SFLOAT,    0 }, // uv
                        { 2, 2, VK_FORMAT_R32G32B32_SFLOAT, 0 }, // norm
                        { 3, 3, VK_FORMAT_R32G32B32_SFLOAT, 0 }, // tan
                    },
                },
                .attachments = {
                    //        albedo                    position                  normal                         pbr
                    .color = {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM},
                    .depth = DEPTH_ATTACHMENT_FORMAT,
                },
                .depthStencil = {
                    .depthTestEnable = true
                },
                .blending = {
                    .attachments = {ALPHA_BLENDING},
                },
            };
            data.shader = e;
            e.emplace<vk::Pipeline>(vk::makePipeline(data.shader.get<vk::Shader>(), layoutCi, pipelineCi));
            data.pipeline = e;
            vk::allocateDescriptors(data.pipeline.getc());
            descManager.addResource(data.pipeline, {0, 0}, uniformBuffer.getBuffer());
            descManager.addResource(data.pipeline, {1, 0}, {images}, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        assert(data.shader.valid() && data.shader.get<vk::Shader>().valid);
        assert(data.pipeline.valid() && data.pipeline.get<vk::Pipeline>().valid);

        data.albedo = res.getResource("gbuffer_albedo");
        data.position = res.getResource("gbuffer_position");
        data.normal = res.getResource("gbuffer_normal");
        data.pbr = res.getResource("gbuffer_pbr");
        data.depth = res.getResource("gbuffer_depth");
    }, [&](gbuffer_data &data, VkCommandBuffer cb){
        auto const &albedo = data.albedo.getc();
        auto const &position = data.position.getc();
        auto const &normal = data.normal.getc();
        auto const &pbr = data.pbr.getc();
        auto const &depth = data.depth.getc();

        auto const &pipeline = data.pipeline.getc();

        VkExtent2D extent = swapchain->createInfo.imageExtent;

        // Update matrix data
        UniformBuffer uniformBufferData;
        uniformBufferData.uMatrixData.camera.projMat = camera.projMat;
        uniformBufferData.uMatrixData.camera.viewMat = camera.viewMat;

        std::array<VkRenderingAttachmentInfo, 4> colorAttachmentInfos = {
            VkRenderingAttachmentInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = albedo.view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue{.color{{ 0.0f, 0.0f, 0.2f, 1.0f }}}
            },
            VkRenderingAttachmentInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = position.view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue{.color{{ 0.0f, 0.0f, 0.0f, 0.0f }}}
            },
            VkRenderingAttachmentInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = normal.view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue{.color{{ 0.5f, 0.5f, 0.5f, 0.0f }}}
            },
            VkRenderingAttachmentInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = pbr.view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue{.color{{ 0.0f, 0.0f, 0.0f, 0.0f }}}
            },
        };
        VkRenderingAttachmentInfo depthAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = depth.view,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = {.depthStencil = {1.0f,  0}}
        };

        VkRenderingInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = { 0, 0 },
                .extent = extent,
            },
            .layerCount = 1,
            .colorAttachmentCount = colorAttachmentInfos.size(),
            .pColorAttachments = colorAttachmentInfos.data(),
            .pDepthAttachment = &depthAttachmentInfo
        };

        vkCmdBeginRendering(cb, &renderingInfo);

        VkViewport vp{
            .x = 0,
            .y = 0,
            .width = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D scissor{ .extent{ .width = window->size.x, .height = window->size.y } };
        vkCmdSetScissor(cb, 0, 1, &scissor);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
        vk::bindDescriptorSet(cb, pipeline, 1);

        for(auto eInstance : sReg.view<ModelInstance>())
        {
            auto const &instance = eInstance.get<ModelInstance>();
            if(!instance.eModel.valid())
            {
                LOG_ERROR("Model instance e has invalid eModel {}", eInstance, instance.eModel);
                continue;
            }
            if(!instance.eModel.has<VulkanModel>())
            {
                LOG_ERROR("Model {} doesent have VulkanModel component!", instance.eModel);
                continue;
            }

            uniformBufferData.uMatrixData.model.modelMat = {1.0f};
            if(eInstance.has<Transform>())
                uniformBufferData.uMatrixData.model.modelMat = eInstance.get<Transform>().getMat();

            uniformBufferData.uMatrixData.model.normMat = glm::transpose(glm::inverse(glm::mat3(uniformBufferData.uMatrixData.model.modelMat)));

            auto &model = instance.eModel.get<VulkanModel>();
            for(auto &mesh : model.meshes)
            {
                uniformBufferData.uMaterial = mesh.material;

                uint32_t offset = uniformBuffer.request(sizeof(uniformBufferData), frameIndex, properties.limits.minUniformBufferOffsetAlignment);
                vk::bindDescriptorSet(cb, pipeline, 0, 0, {offset});
                std::memcpy(static_cast<char *>(uniformBuffer.getBuffer().get<vk::Buffer>().mapped) + offset, &uniformBufferData, sizeof(uniformBufferData));

                VkDeviceSize vOffset = 0;
                vkCmdBindVertexBuffers(cb, 0, 1, &mesh.buffers.pos .buffer, &vOffset);
                vkCmdBindVertexBuffers(cb, 1, 1, &mesh.buffers.uv  .buffer, &vOffset);
                vkCmdBindVertexBuffers(cb, 2, 1, &mesh.buffers.norm.buffer, &vOffset);
                vkCmdBindVertexBuffers(cb, 3, 1, &mesh.buffers.tan .buffer, &vOffset);
                vkCmdBindIndexBuffer(cb, mesh.buffers.idx.buffer, 0, VK_INDEX_TYPE_UINT32);
        
                vkCmdDrawIndexed(cb, mesh.indexCount, 1, 0, 0, 0);
            }
        }

        vkCmdEndRendering(cb);
    });
    struct lighting_data {
        DirectEntity<vk::Pipeline> pipeline;
        DirectEntity<vk::Shader> shader;

        DirectEntity<vk::Image> output;
    };
    builder.addPass<lighting_data>("lighting", VK_QUEUE_GRAPHICS_BIT, [&](RenderPassBuilder &builder){
        auto extent = swapchain->createInfo.imageExtent;
        builder.addImageResource("lighting_out", {.imageInfo = {
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .dimensions = {extent.width, extent.height},
            .view = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT }
        }, .resizeToSwapchain = true });
        builder.attachResourceRead("gbuffer_albedo",   SHADER_READ_TRAITS);
        builder.attachResourceRead("gbuffer_position", SHADER_READ_TRAITS);
        builder.attachResourceRead("gbuffer_normal",   SHADER_READ_TRAITS);
        builder.attachResourceRead("gbuffer_pbr",      SHADER_READ_TRAITS);
        builder.attachResourceWrite("lighting_out",    COLOR_ATTACHMENT_TRAITS);
    }, [&](lighting_data &data, RenderGraphResult const &res){
        if(!data.shader.valid()) {
            auto e = sReg.create();
            e.emplace<vk::Shader>(vk::makeShader({
                .backend = vk::ShaderBackend::SLANG,
                .src = "shaders/deferred/lighting.slang",
                .bin = "shaders-bin/deferred/lighting.slang",
                .device = device,
                .includeDirs = {"shaders"},
                .debugInfo = true
            }));
            vk::PipelineLayoutCreateInfo layoutCi{
                .dynamicDescriptors = {{.set = 1, .binding = 0}},
            };
            vk::GraphicsPipelineCreateInfo pipelineCi{
                .dynamicState = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR },
                .attachments = {
                    .color = {VK_FORMAT_R16G16B16A16_SFLOAT},
                    .depth = DEPTH_ATTACHMENT_FORMAT,
                },
                .blending = {
                    .attachments = {NO_BLENDING},
                },
            };
            data.shader = e;
            e.emplace<vk::Pipeline>(vk::makePipeline(data.shader.get<vk::Shader>(), layoutCi, pipelineCi));
            data.pipeline = e;
            vk::allocateDescriptors(data.pipeline.getc());
            descManager.addResource(data.pipeline, {0, 0}, uniformBuffer.getBuffer());
            descManager.addResource(data.pipeline, {1, 0}, {images}, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        assert(data.shader.valid() && data.shader.get<vk::Shader>().valid);
        assert(data.pipeline.valid() && data.pipeline.get<vk::Pipeline>().valid);

        descManager.addResource(data.pipeline, {0, 0}, res.getResource("gbuffer_albedo"), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        descManager.addResource(data.pipeline, {0, 1}, res.getResource("gbuffer_position"), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        descManager.addResource(data.pipeline, {0, 2}, res.getResource("gbuffer_normal"), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        descManager.addResource(data.pipeline, {0, 3}, res.getResource("gbuffer_pbr"), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        data.output = res.getResource("lighting_out");
    }, [&](lighting_data &data, VkCommandBuffer cb){
        auto const &out = data.output.getc();

        auto const &pipeline = data.pipeline.getc();

        VkExtent2D extent = swapchain->createInfo.imageExtent;

        // Update matrix data
        UniformBuffer uniformBufferData;
        uniformBufferData.uMatrixData.camera.projMat = camera.projMat;
        uniformBufferData.uMatrixData.camera.viewMat = camera.viewMat;

        std::array<VkRenderingAttachmentInfo, 1> colorAttachmentInfos = {
            VkRenderingAttachmentInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = out.view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue{.color{{ 0.0f, 0.0f, 0.0f, 0.0f }}}
            },
        };

        VkRenderingInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = { 0, 0 },
                .extent = extent,
            },
            .layerCount = 1,
            .colorAttachmentCount = colorAttachmentInfos.size(),
            .pColorAttachments = colorAttachmentInfos.data(),
            .pDepthAttachment = nullptr
        };

        vkCmdBeginRendering(cb, &renderingInfo);

        VkViewport vp{
            .x = 0,
            .y = 0,
            .width = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D scissor{ .extent{ .width = window->size.x, .height = window->size.y } };
        vkCmdSetScissor(cb, 0, 1, &scissor);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
        vk::bindDescriptorSet(cb, pipeline, 1);

        vkCmdDraw(cb, 3, 1, 0, 0);

        vkCmdEndRendering(cb);
    });

    struct swapchain_blit_data {
        DirectEntity<vk::Image> lighting_out;
        DirectEntity<vk::Image> swapchainImage;
    };
    builder.addPass<swapchain_blit_data>("swapchain_blit", VK_QUEUE_TRANSFER_BIT, [&](RenderPassBuilder &builder){
        builder.addExternalResource("swapchain", swapchain->images[imageIndex]);
        builder.attachResourceRead("lighting_out",  TRANSFER_SRC_TRAITS);
        builder.attachResourceWrite("swapchain",  TRANSFER_DST_TRAITS);
    }, [&](swapchain_blit_data &data, RenderGraphResult const &res){
        data.swapchainImage = res.getResource("swapchain");
        data.lighting_out = res.getResource("lighting_out");
    }, [&](swapchain_blit_data &data, VkCommandBuffer cb){
        auto const &src = data.lighting_out.getc();
        auto const &dst = data.swapchainImage.getc();

        VkImageBlit2 region{
            .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .srcOffsets = {{0, 0, 0}, {static_cast<int32_t>(src.createInfo.image.dimensions.width), static_cast<int32_t>(src.createInfo.image.dimensions.height), 1}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .dstOffsets = {{0, 0, 0}, {static_cast<int32_t>(dst.createInfo.image.dimensions.width), static_cast<int32_t>(dst.createInfo.image.dimensions.height), 1}},
        };
        VkBlitImageInfo2 blit{
            .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
            .srcImage = src.image,
            .srcImageLayout = TRANSFER_SRC_TRAITS.imageTraits.layout,
            .dstImage = dst.image,
            .dstImageLayout = TRANSFER_DST_TRAITS.imageTraits.layout,
            .regionCount = 1,
            .pRegions = &region,
            .filter = VK_FILTER_NEAREST
        };
        vkCmdBlitImage2(cb, &blit);

        // Prepare for presentation
        vk::insertImageMemoryBarrier(cb, dst.image, 
            TRANSFER_DST_TRAITS.access,
            0,
            TRANSFER_DST_TRAITS.imageTraits.layout,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            TRANSFER_DST_TRAITS.stages,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        );
    });

    RenderGraphResult renderGraph = buildRenderGraph(std::move(builder));
    if(!renderGraph.success()) {
        LOG_ERROR("Failed to build a frame graph!");
        return -1;
    }
    std::ofstream("RenderGraph.dot", std::ios::trunc) << renderGraph.dumpGraphviz(2);

    ////////////////////////////////////////////////////////////////

    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> presentSemaphores;
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> fences;
    std::vector<VkSemaphore> renderSemaphores;
    SparseSet<std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT>> commandBuffers;
    std::unordered_set<uint32_t> usedCommandBuffers;

    for(auto queue : initRes.queueFamilies.uniqueFamilies)
    {
        VkCommandBufferAllocateInfo commandBufferAllocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
        };
    
        CHECK_VK_RES(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, commandBuffers[queue].data()));

        for(uint i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            auto cb = commandBuffers[queue][i];
            auto name = fmt::format("cb #{} {}", i, string_VkQueueFlags(queue));
            VkDebugUtilsObjectNameInfoEXT name_info{
                .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .objectType   = VK_OBJECT_TYPE_COMMAND_BUFFER,
                .objectHandle = (uint64_t) cb,
                .pObjectName  = name.c_str(),
            };
            vkSetDebugUtilsObjectNameEXT(device, &name_info);
        }
    }

    for(uint i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) 
    {
        VkSemaphoreCreateInfo semaphoreCI{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .flags = 0
        };
        vkCreateSemaphore(device, &semaphoreCI, nullptr, &presentSemaphores[i]);

        VkFenceCreateInfo fenceCI{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT
        };

        CHECK_VK_RES(vkCreateFence(device, &fenceCI, nullptr, &fences[i]));
    }

    renderSemaphores.resize(swapchain->images.size());
    for(auto &semaphore : renderSemaphores)
    {
        VkSemaphoreCreateInfo semaphoreCI{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };
        vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore);
    }
    
    Controller cameraController;
    auto eListener = sReg.create<io::EventListener>();

    VkQueue presentQueue;
    assert(initRes.queueFamilies.presentQueue.has_value());
    vkGetDeviceQueue(device, initRes.queueFamilies.presentQueue.value(), 0, &presentQueue);

    LOG_INFO("Starting rendering.");

    bool shouldResize = false;
    while(!glfwWindowShouldClose(window->handle))
    {
        auto start = std::chrono::high_resolution_clock::now();
        // LOG_TRACE(frameIndex);
        
        // Poll events
        glfwPollEvents();
        auto prevSize = window->size;
        glfwGetWindowSize(window->handle, reinterpret_cast<int *>(&window->size.x), reinterpret_cast<int *>(&window->size.y));
        VkExtent2D windowExtent = { .width = window->size.x, .height = window->size.y };

        shouldResize = shouldResize || prevSize != window->size;
        
        // Resize swapchain
        if(shouldResize) {
            LOG_WARN("Resizing the viewport to {}x{}", windowExtent.width, windowExtent.height);
            CHECK_VK_RES(vkDeviceWaitIdle(device));

            vk::resizeSwapchain(swapchain.getc(), windowExtent);
 
            for(auto e : sReg.view<vk::Image, ResizeToSwapchain>())
            {
                auto &image = e.get<vk::Image>();
                vk::destroy(image);
                image.createInfo.image.dimensions.width = windowExtent.width;
                image.createInfo.image.dimensions.height = windowExtent.height;
                image = vk::makeImage(image.createInfo);
            }

            shouldResize = false;
        }
        
        auto &listener = eListener.get<io::EventListener>();
        while(!listener.keyEvents.empty())
        {
            auto event = listener.keyEvents.front();
            listener.keyEvents.pop();

            if(event.key == GLFW_KEY_R && event.action == GLFW_PRESS)
            {
                CHECK_VK_RES(vkDeviceWaitIdle(device));
                for(auto e : sReg.view<vk::Pipeline, vk::Shader>())
                {
                    auto &pipeline = e.get<vk::Pipeline>();
                    auto &shader = e.get<vk::Shader>();

                    auto newShader = vk::makeShader(shader.createInfo);
                    if(!newShader.valid)
                        continue;

                    vk::destroy(shader);
                    shader = newShader;
                    vk::destroy(pipeline);
                    switch(pipeline.type)
                    {
                    case vk::Pipeline::Type::Graphics:
                    pipeline = vk::makePipeline(shader, pipeline.createInfo.layout, pipeline.createInfo.graphics);
                    break;

                    case vk::Pipeline::Type::Compute:
                    pipeline = vk::makePipeline(shader, pipeline.createInfo.layout, pipeline.createInfo.compute);
                    break;

                    case vk::Pipeline::Type::RayTracing:
                    pipeline = vk::makePipeline(shader, pipeline.createInfo.layout, pipeline.createInfo.raytracing);
                    break;

                    default:
                    assert(false && "unknown pipeline");
                    }
                    vk::allocateDescriptors(pipeline);

                    e.emplace<ResourceDirty>();
                }
            }
        }

        cameraController.update(sReg, deltatime);

/////////////////////////////////////////////////////////////////////

        // Wait on fence
        CHECK_VK_RES(vkWaitForFences(device, 1, &fences[frameIndex], true, UINT64_MAX));
        CHECK_VK_RES(vkResetFences(device, 1, &fences[frameIndex]));
        
        uniformBuffer.free(frameIndex);
        uniformBuffer.realloc();

        descManager.update(frameIndex);
        for(auto e : sReg.view<ResourceDirty>()) {
            e.remove<ResourceDirty>();
        }

        // Acquire next image
        auto imageAcquireRes = vkAcquireNextImageKHR(device, swapchain->swapchain, UINT64_MAX, presentSemaphores[frameIndex], nullptr, &imageIndex);
        if(imageAcquireRes == VK_ERROR_OUT_OF_DATE_KHR)
        {
            LOG_TRACE("VK_ERROR_OUT_OF_DATE_KHR");
            shouldResize = true;
            continue;
        } else if(imageAcquireRes == VK_SUBOPTIMAL_KHR) {
            // Ignore
            // FIXME: Infinite VK_SUBOPTIMAL_KHR (on wayland)
            // LOG_TRACE("VK_SUBOPTIMAL_KHR");
            // shouldResize = true;
        } else if(imageAcquireRes != VK_SUCCESS)
        {
            CHECK_VK_RES(imageAcquireRes);
            LOG_ERROR("Could not acquire the next swap chain image!");
            break;
        } 

        //                                   This is so dumb
        renderGraph.setResource("swapchain", swapchain->images[imageIndex]);

        // Record command buffer
        // FUUUUCK
        for(auto passIndex : renderGraph.getPassStack())
        {
            auto pass = renderGraph.getPass(passIndex);
            assert(pass);

            struct Barriers {
                std::vector<VkImageMemoryBarrier2> imageBarriers;
                std::vector<VkBufferMemoryBarrier2> bufferBarriers;
                inline void emplace(Barrier const &barrier, RenderGraphResult const &rg) {
                    auto eResource = rg.getResource(barrier.resource);
                    if(eResource.has<vk::Image>())
                        imageBarriers.emplace_back(barrier.getImageBarrier(eResource));
                    if(eResource.has<vk::Buffer>())
                        bufferBarriers.emplace_back(barrier.getBufferBarrier(eResource));
                }
            };
            // For each queue for release/acquire operations
            SparseSet<Barriers> queueBarriers;
            for(auto barrier : pass->barriers)
            {
                if(barrier.src.queueIndex == VK_QUEUE_FAMILY_IGNORED)
                    barrier.src.queueIndex = barrier.dst.queueIndex;

                if(barrier.src.queueIndex != barrier.dst.queueIndex)
                    queueBarriers[barrier.src.queueIndex].emplace(barrier, renderGraph);
                
                queueBarriers[barrier.dst.queueIndex].emplace(barrier, renderGraph);
            }


            auto queue = initRes.queueFamilies.indices.at(pass->queue);
            
            auto queues = queueBarriers.sparse();
            queues.emplace_back(queue);
            for(auto queue : queues) {
                if(!usedCommandBuffers.contains(queue)) {
                    auto cb = commandBuffers.at(queue)[frameIndex];

                    usedCommandBuffers.emplace(queue);
                    CHECK_VK_RES(vkResetCommandBuffer(cb, 0));
                    VkCommandBufferBeginInfo cbBI{
                        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
                    };
                    CHECK_VK_RES(vkBeginCommandBuffer(cb, &cbBI));
                }
            }

            for(auto [queue, barrierss] : queueBarriers)
            {
                VkDependencyInfo dependencyInfo{
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .bufferMemoryBarrierCount = static_cast<uint32_t>(barrierss.bufferBarriers.size()),
                    .pBufferMemoryBarriers = barrierss.bufferBarriers.data(),
                    .imageMemoryBarrierCount = static_cast<uint32_t>(barrierss.imageBarriers.size()),
                    .pImageMemoryBarriers = barrierss.imageBarriers.data()
                };
                vkCmdPipelineBarrier2(commandBuffers.at(queue)[frameIndex], &dependencyInfo);
            }

            pass->render(commandBuffers.at(queue)[frameIndex]);
        }

        // WHY?
        static std::vector<VkCommandBuffer> submitCommandBuffers;
        submitCommandBuffers.clear();
        submitCommandBuffers.reserve(usedCommandBuffers.size());
        for(auto queue : usedCommandBuffers)
        {
            auto cb = commandBuffers.at(queue)[frameIndex];
            vkEndCommandBuffer(cb);
            submitCommandBuffers.emplace_back(cb);
        }
        usedCommandBuffers.clear();

        // Submit command buffer
        // TODO: figure this out
        VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &presentSemaphores[frameIndex],
            .pWaitDstStageMask = &waitStages,
            .commandBufferCount = static_cast<uint32_t>(submitCommandBuffers.size()),
            .pCommandBuffers = submitCommandBuffers.data(),
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &renderSemaphores[imageIndex],
        };
        CHECK_VK_RES(vkQueueSubmit(graphicsQueue, 1, &submitInfo, fences[frameIndex]));

        frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
        
        // Present image
        VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderSemaphores[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &swapchain->swapchain,
            .pImageIndices = &imageIndex
        };
        CHECK_VK_RES(vkQueuePresentKHR(presentQueue, &presentInfo));

        deltatime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start).count() * 1e-9f;
        // LOG_INFO("dt {:.2f}ms fps {:.2f}", deltatime * 1e3, 1 / deltatime);
    }

    CHECK_VK_RES(vkDeviceWaitIdle(device));

    ////////////////////////////////////////////////////////////////

    for(uint i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) 
    {
        vkDestroySemaphore(device, presentSemaphores[i], nullptr);
        vkDestroyFence(device, fences[i], nullptr);
    }
    for(auto &semaphore : renderSemaphores)
        vkDestroySemaphore(device, semaphore, nullptr);

    vkDestroyCommandPool(device, commandPool, nullptr);

    for(auto e : sReg.view<VulkanModel>())
    {
        for(auto &mesh : e.get<VulkanModel>().meshes)
        {
            vk::destroy(mesh.buffers.pos);
            vk::destroy(mesh.buffers.uv);
            vk::destroy(mesh.buffers.norm);
            vk::destroy(mesh.buffers.tan);
            vk::destroy(mesh.buffers.idx);
        }
    }
    for(auto e : sReg.view<vk::Image>()) {
        vk::destroy(e.get<vk::Image>());
        e.destroy();
    }
    for(auto e : sReg.view<vk::Buffer>()) {
        vk::destroy(e.get<vk::Buffer>());
        e.destroy();
    }
    for(auto e : sReg.view<vk::Pipeline>()) {
        vk::destroy(e.get<vk::Pipeline>());
        e.destroy();
    }
    for(auto e : sReg.view<vk::Shader>()) {
        vk::destroy(e.get<vk::Shader>());
        e.destroy();
    }
    
    vk::destroy(swapchain.getc());
    swapchain.destroy();

    LOG_INFO("Exiting");
    return 0;
}
int main(int argc, char **argv) {
    initLogger();

    CPPTRACE_TRY {
        return app(argc, argv);
    } CPPTRACE_CATCH(std::exception const &e) {
        LOG_ERROR("Exception: {}\n{}", e.what(), cpptrace::from_current_exception().to_string(true));
    }
}   