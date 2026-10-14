#include <bits/stdc++.h>

#include "engine/scene/Animation.hpp"
#include "engine/dsa/ECS.hpp"
#include "cpptrace/from_current.hpp"
#include "engine/vk/vk.hpp"

#include "Controller.hpp"
#include "engine/io/Logging.hpp"
#include "engine/renderer/Renderdoc.hpp"
#include "engine/renderer/Renderer.hpp"
#include "engine/renderer/RenderGraph.hpp"
#include "engine/scene/Resources.hpp"

#include "tracy/Tracy.hpp"
#include "tracy/TracyVulkan.hpp"

egn::Registry sReg;
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

template <typename T> static T hash_combine(T lhs, T rhs) {
    lhs ^= rhs + T(0x9e3779b9) + (lhs << T(6)) + (lhs >> T(2));
    return lhs;
}
static std::string getDateTime() {
    time_t timestamp = time(NULL);
    struct tm datetime = *localtime(&timestamp);
    std::string str = asctime(&datetime);
    str.pop_back(); // pop newline
    return str;
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
static egn::Entity loadTexture(std::string_view path, bool required = true, bool flip = false) {
    static egn::TextureLoader loader(sReg.getReg());

    egn::TextureLoaderOptions options{
        .flip = flip,
        .desiredChannels = 4,
    };
    auto eTexture = egn::Entity{ &sReg, loader.loadFromFile(path, options) };
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
static egn::Entity loadModel(std::string_view path, std::optional<egn::Material::Textures> textures = { }, bool required = true) {
    static egn::ModelLoader loader(sReg.getReg());
    static auto smoothPaths = getSmoothShadedModelPaths();

    egn::ModelLoaderOptions options{
        .flipUVs = false,
        .flipHandiness = true, // ??
        .windingOrder = egn::ModelLoaderOptions::WindingOrder::CCW,
        .smoothNormalAngle = static_cast<float>(smoothPaths.contains(std::string(path)) ? 35 : 0),
    };
    auto eModel = egn::Entity{ &sReg, loader.loadFromFile(path, options) };
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
static bool loadModelTo(std::string_view path, std::vector<egn::Entity> &to, std::optional<egn::Material::Textures> textures = { }) {
    auto e = loadModel(path, textures, false);
    if(e) {
        to.emplace_back(e);
    }

    return e;
}
static void dumpModels(std::string_view dir) {
    std::ofstream modelStr(std::string(dir.data()) + "/models.txt");
    for(egn::RestrictedEntity<egn::Model> e : sReg.view<egn::Model>()) {
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
static egn::Entity makeWindow(egn::Registry &reg, std::string_view name) {
    auto eWindow = reg.create(egn::Window{ }, egn::DebugName("window_" + std::string(name)));
    auto &window = eWindow.get<egn::Window>();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window.handle = glfwCreateWindow(800, 600, name.data(), nullptr, nullptr);
    glfwGetWindowSize(window.handle, reinterpret_cast<int *>(&window.size.x), reinterpret_cast<int *>(&window.size.y));
    glfwSwapInterval(1);
    if(glfwRawMouseMotionSupported())
        glfwSetInputMode(window.handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    egn::setCallbacks(window.handle, reg.getReg());

    return eWindow;
}
static VkCommandPool createCommandPool(uint32_t index, VkDevice device) {
    VkCommandPoolCreateInfo commandPoolCI{ .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = index };
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
static void assetGrid(std::vector<egn::Entity> const &props, glm::uvec3 dimensions, glm::vec3 offset, float distance) {
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
                sReg.create(egn::ModelInstance{ props[distP(rng)] }, lookat(position(x, y, z), position(distX(rng), distY(rng), distZ(rng))));
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

////////////////////////////////////////////////////////////////

static bool init() {
    if(!glfwInit()) {
        LOG_ERROR("Failed to init glfw!");
        return false;
    }
    if(!glfwVulkanSupported()) {
        LOG_ERROR("Vulkan is not supported!");
        return false;
    }

    VkResult res = volkInitialize();
    if(res != VK_SUCCESS) {
        LOG_ERROR("Failed to init volk: {}!", string_VkResult(res));
        return false;
    }

    return true;
}
int app([[maybe_unused]] int argc, [[maybe_unused]] char **argv) {
    if(!init()) {
        LOG_ERROR("Failed to init!");
        return -1;
    }

    egn::RestrictedEntity<egn::Window> window = makeWindow(sReg, "levulkan");

    egn::vk::InitInfo initInfo{
        .appName = "levulkan",
        .window = window->handle,
        .version = VK_API_VERSION_1_4,
        .deviceExtensions = { VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME },
        .queues = {VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_TRANSFER_BIT},
        .deviceFeatures = {
            .features =
                {
                    .geometryShader = true,
                    .shaderSampledImageArrayDynamicIndexing = true,
                },
            .vulkan12 =
                {
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

    egn::vk::enableValidationLayers(initInfo);

    [[maybe_unused]] static class VulkanInitializer {
    private:
        egn::vk::InitResult initRes;
    public:
        inline VulkanInitializer(egn::vk::InitInfo const &initInfo) { initRes = egn::vk::init(initInfo); }
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

        inline egn::vk::InitResult const &getInitRes() const { return initRes; }
        inline egn::vk::InitResult &getInitRes() { return initRes; }
    } vulkanInitializer(initInfo);
    egn::vk::InitResult const &initRes = vulkanInitializer.getInitRes();
    if(!initRes.success) {
        LOG_ERROR("Failed to init vulkan!");
        return -1;
    }
    auto &device = initRes.device;

    VkSharingMode const SHARING_MODE = initRes.queueFamilies.uniqueFamilies.size() == 1 ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
    LOG_TRACE("SHARING_MODE={}", string_VkSharingMode(SHARING_MODE));

    egn::RestrictedEntity<egn::vk::Swapchain> swapchain = sReg.create(egn::vk::makeSwapchain(
        {
            .alloc = {
                .device = device,
                .physicalDevice = initRes.physicalDevice,
                .surface = initRes.surface,
            },
            .size = {window->size.x, window->size.y},
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .registry = &sReg,
        }),
        egn::DebugName("swapchain_" + (window.contains<egn::DebugName>() ? window.get<egn::DebugName>().name : "")));

    VkCommandPool commandPool = createCommandPool(initRes.queueFamilies.indices.at(VK_QUEUE_GRAPHICS_BIT), device);

    egn::vk::AllocationCreateInfo const ALLOCATION_INFO{ .device = initRes.device, .allocator = initRes.vma, .sharingMode = SHARING_MODE };

    ////////////////////////////////////////////////////////////////

    { // Scene
        const auto prototypeTextures = egn::Material::Textures{ .albedo = loadTexture("assets/textures/prototype/texture_03.png", false) };
        std::vector<egn::Entity> props;
        loadModelTo("assets/models/suzanne.glb", props, prototypeTextures);
        loadModelTo("assets/models/cube.glb", props, prototypeTextures);
        loadModelTo("assets/models/sphere.glb", props, prototypeTextures);
        loadModelTo("assets/models/teapot.glb", props, prototypeTextures);

        const glm::uvec3 numProps = { 10, 3, 5 };
        const float distance = 2;
        const glm::vec3 offset = { 0, -1, -5 };
        assetGrid(props, numProps, offset, distance);

        const glm::vec3 startPos(0, -4, 0);
        const glm::vec3 step(2, 0, 0);
        for(uint i = 0; i < props.size(); ++i) {
            sReg.create(egn::ModelInstance{ props[i] }, egn::Transform{ .position = startPos + static_cast<float>(i) * step });
        }

        const auto cubes = loadModel("assets/models/deccer_cubes/SM_Deccer_Cubes_Textured_Complex.gltf", { });
        if(cubes) {
            sReg.create(egn::ModelInstance{ cubes }, egn::Transform{ .position = { -10, 0, 10 } });
        }

        auto fox = loadModel("assets/models/fox.glb");
        struct CycleAnimations {};
        fox.emplace<CycleAnimations>();
        fox.emplace<egn::ModelInstance>(fox);

        dumpModels("tmp/models");
    }

    ////////////////////////////////////////////////////////////////

    egn::ResourceAllocator allocator(ALLOCATION_INFO, commandPool, initRes.queueFamilies.getQueue(VK_QUEUE_GRAPHICS_BIT));
    allocator.begin();
    for(auto eTexture : sReg.view<egn::Texture2D>(egn::exclude<egn::vk::Image>{ })) {
        allocator.processImage(eTexture);
    }
    for(auto eTexture : sReg.view<egn::Model>(egn::exclude<egn::VulkanModel>{ })) {
        allocator.processModel(eTexture);
    }
    allocator.end();

    auto images = allocator.getProcessedImages();
    std::vector<egn::ImageDescriptorWrite> imageInfos;
    imageInfos.reserve(images.size());
    for(auto eImage : images) {
        imageInfos.emplace_back(eImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    egn::DescriptorManager descManager;

    egn::vk::RingBuffer uniformBuffer(sReg,
        {
            .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            .allocInfo = ALLOCATION_INFO,
            .size = static_cast<uint32_t>(64 * 1e6), // 64MB
            .map = true,
            .name = "uniform_buffer",
        });

    VkPhysicalDeviceProperties2 properties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    vkGetPhysicalDeviceProperties2(initRes.physicalDevice, &properties);

    ////////////////////////////////////////////////////////////////

    uint frameIndex = 0;
    uint imageIndex = 0;
    float deltatime = 1e-6;

    egn::RestrictedEntity<Controller::Camera> eCamera = Controller::createCamera(sReg, { 0, 2, 4 }, { 0, 0, 0 });

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

    // TODO: A better test render pipeline that uses all of the render graph features
    // Multiple queues
    // Complex pass dependencies
    // Resource aliasing
    // History resources
    // Per frame in flight resource descriptors
    // Compute buffers
    egn::RenderGraphBuilder builder;
    builder.setAllocInfo(ALLOCATION_INFO, sReg);
    builder.setQueueFamilies(initRes.queueFamilies);

    TracyVkCtx *currentTracyContext = nullptr;

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
            auto extent = swapchain.get<egn::vk::Swapchain>().createInfo.imageExtent;
            builder.addImageResource("gbuffer_albedo", {
                .imageInfo = {
                    .format = VK_FORMAT_R8G8B8A8_UNORM,
                    .dimensions = {extent.width, extent.height},
                    .view = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
                },
                .resizeToSwapchain = true,
            });
            builder.addImageResource("gbuffer_position", {
                .imageInfo = {
                    .format = VK_FORMAT_R8G8B8A8_UNORM,
                    .dimensions = {extent.width, extent.height},
                    .view = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
                },
                .resizeToSwapchain = true,
            });
            builder.addImageResource("gbuffer_normal", {
                .imageInfo = {
                    .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                    .dimensions = {extent.width, extent.height},
                    .view = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
                },
                .resizeToSwapchain = true,
            });
            builder.addImageResource("gbuffer_pbr", {
                .imageInfo = {
                    .format = VK_FORMAT_R8G8B8A8_UNORM,
                    .dimensions = {extent.width, extent.height},
                    .view = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT},
                },
                .resizeToSwapchain = true,
            });
            builder.addImageResource("gbuffer_depth", { 
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
                egn::Entity e = sReg.create();
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
                e.emplace<egn::DebugName>("gbuffer_pipeline");
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
            assert(currentTracyContext);
            TracyVkZone(*currentTracyContext, cb, "G Buffer");
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

            for(auto eInstance : sReg.view<egn::ModelInstance>()) {
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
            builder.addImageResource("lighting_out", {
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
                auto e = sReg.create();
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
                e.emplace<egn::DebugName>("lighting_pipeline");
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
            assert(currentTracyContext);
            TracyVkZone(*currentTracyContext, cb, "Lighting");
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
        egn::RestrictedEntity<egn::vk::Image> swapchainImage;
    };
    builder.addPass<swapchain_blit_data>(
        "swapchain_blit",
        VK_QUEUE_TRANSFER_BIT,
        [&](egn::RenderPassBuilder &builder) {
            builder.addExternalResource("swapchain", swapchain->images[imageIndex]);
            builder.attachResourceRead("lighting_out", TRANSFER_SRC_TRAITS);
            builder.attachResourceWrite("swapchain", TRANSFER_DST_TRAITS);
        },
        [&](swapchain_blit_data &data, egn::RenderGraphResult const &res) {
            data.swapchainImage = res.getResource("swapchain");
            data.lighting_out = res.getResource("lighting_out");
        },
        [&](swapchain_blit_data &data, VkCommandBuffer cb) {
            assert(currentTracyContext);
            TracyVkZone(*currentTracyContext, cb, "Swapchain blit");
            auto const &src = data.lighting_out.getc();
            auto const &dst = data.swapchainImage.getc();

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

    egn::RenderGraphResult renderGraph = buildRenderGraph(std::move(builder));
    if(!renderGraph.success()) {
        LOG_ERROR("Failed to build a frame graph!");
        return -1;
    }
    std::ofstream("tmp/RenderGraph.dot", std::ios::trunc) << renderGraph.dumpGraphviz(2);

    ////////////////////////////////////////////////////////////////

    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> presentSemaphores;
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> fences;
    std::vector<VkSemaphore> renderSemaphores;
    egn::SparseSet<std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT>> commandBuffers;
    std::unordered_set<uint32_t> usedCommandBuffers;

    [[maybe_unused]] egn::SparseSet<std::array<TracyVkCtx, MAX_FRAMES_IN_FLIGHT>> tracyContexts;
    for(auto queue : initRes.queueFamilies.uniqueFamilies) {
        VkCommandBufferAllocateInfo commandBufferAllocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
        };

        CHECK_VK_RES(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, commandBuffers[queue].data()));

        for(uint i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            auto cb = commandBuffers[queue][i];
            auto name = fmt::format("cb #{} {}", i, string_VkQueueFlags(queue));

            VkQueue physQueue;
            vkGetDeviceQueue(device, queue, 0, &physQueue);
            [[maybe_unused]] auto &ctx = tracyContexts[queue][i] = (TracyVkContextCalibrated(initRes.physicalDevice,
                initRes.device,
                physQueue,
                cb,
                vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
                vkGetCalibratedTimestampsEXT));
            TracyVkContextName(ctx, name.data(), name.size());
            VkDebugUtilsObjectNameInfoEXT name_info{
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                .objectType = VK_OBJECT_TYPE_COMMAND_BUFFER,
                .objectHandle = (uint64_t) cb,
                .pObjectName = name.c_str(),
            };
            vkSetDebugUtilsObjectNameEXT(device, &name_info);
        }
    }

    for(uint i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkSemaphoreCreateInfo semaphoreCI{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .flags = 0 };
        vkCreateSemaphore(device, &semaphoreCI, nullptr, &presentSemaphores[i]);

        VkFenceCreateInfo fenceCI{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        CHECK_VK_RES(vkCreateFence(device, &fenceCI, nullptr, &fences[i]));
    }

    renderSemaphores.resize(swapchain->images.size());
    for(auto &semaphore : renderSemaphores) {
        VkSemaphoreCreateInfo semaphoreCI{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore);
    }

    Controller cameraController;
    auto eListener = sReg.create<egn::EventListener>();

    VkQueue presentQueue;
    assert(initRes.queueFamilies.presentQueue.has_value());
    vkGetDeviceQueue(device, initRes.queueFamilies.presentQueue.value(), 0, &presentQueue);

    // TODO: Better shader/pipeline creation + better uniform buffer

    LOG_INFO("Starting rendering.");

    bool shouldResize = false;
    uint64_t frameCount = 0;
    double runtime = 0;
    while(!glfwWindowShouldClose(window->handle)) {
        FrameMark;

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
            LOG_INFO("Resizing the swapchain and {} image resources to {}x{}",
                sReg.view<egn::vk::Image, egn::ResizeToSwapchain>().size(),
                windowExtent.width,
                windowExtent.height);
            CHECK_VK_RES(vkDeviceWaitIdle(device));

            egn::vk::resizeSwapchain(swapchain.getc(), windowExtent);

            for(auto e : sReg.view<egn::vk::Image, egn::ResizeToSwapchain>()) {
                auto &image = e.get<egn::vk::Image>();
                egn::vk::destroy(image);
                image.createInfo.image.dimensions.width = windowExtent.width;
                image.createInfo.image.dimensions.height = windowExtent.height;
                image = egn::vk::makeImage(image.createInfo);
                e.tryEmplace<egn::ResourceDirty>();
            }

            shouldResize = false;
        }

        auto &listener = eListener.get<egn::EventListener>();
        while(!listener.keyEvents.empty()) {
            auto event = listener.keyEvents.front();
            listener.keyEvents.pop();

            if(event.key == GLFW_KEY_R && event.action == GLFW_PRESS) {
                CHECK_VK_RES(vkDeviceWaitIdle(device));
                for(auto e : sReg.view<egn::vk::Pipeline, egn::vk::Shader>()) {
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

        cameraController.update(sReg, deltatime);

        for(auto e : sReg.view<egn::ModelInstance, egn::CurrentAnimation>()) {
            animate(e, deltatime);
        }

        /////////////////////////////////////////////////////////////////////

        // Wait on fence
        CHECK_VK_RES(vkWaitForFences(device, 1, &fences[frameIndex], true, UINT64_MAX));
        CHECK_VK_RES(vkResetFences(device, 1, &fences[frameIndex]));

        // Acquire next image
        auto imageAcquireRes = vkAcquireNextImageKHR(device, swapchain->swapchain, UINT64_MAX, presentSemaphores[frameIndex], nullptr, &imageIndex);
        if(imageAcquireRes == VK_ERROR_OUT_OF_DATE_KHR) {
            LOG_TRACE("VK_ERROR_OUT_OF_DATE_KHR");
            shouldResize = true;
            continue;
        } else if(imageAcquireRes == VK_SUBOPTIMAL_KHR) {
            // Ignore
            // FIXME: Infinite VK_SUBOPTIMAL_KHR (on wayland)
            // LOG_TRACE("VK_SUBOPTIMAL_KHR");
            // shouldResize = true;
        } else if(imageAcquireRes != VK_SUCCESS) {
            CHECK_VK_RES(imageAcquireRes);
            LOG_ERROR("Could not acquire the next swap chain image!");
            break;
        }

        //                                   This is so dumb
        renderGraph.setResource("swapchain", swapchain->images[imageIndex]);
        for(auto const &passName : renderGraph.getPassStack()) {
            auto const &pass = renderGraph.getPass(passName);
            pass->storage->postCompile(renderGraph);
        }

        uniformBuffer.free(frameIndex);
        uniformBuffer.realloc();

        if(sReg.view<egn::ResourceDirty>().size())
            LOG_TRACE("{} resources are dirty! {}", sReg.view<egn::ResourceDirty>().size(), sReg.view<egn::ResourceDirty>());

        // FIXME: ResourceDirty flag removes too early for resources that are used in multiple frames.
        // FIXME: writes are added only for frame 0
        descManager.update(0);
        // FIXME: shit
        for(auto e : sReg.view<egn::ResourceDirty>()) {
            e.erase<egn::ResourceDirty>();
        }

        // Record command buffer
        // FUUUUCK
        for(auto passIndex : renderGraph.getPassStack()) {
            auto pass = renderGraph.getPass(passIndex);
            assert(pass);

            struct Barriers {
                std::vector<VkImageMemoryBarrier2> imageBarriers;
                std::vector<VkBufferMemoryBarrier2> bufferBarriers;
                inline void emplace(egn::Barrier const &barrier, egn::RenderGraphResult const &rg) {
                    auto eResource = rg.getResource(barrier.resource);
                    if(eResource.contains<egn::vk::Image>())
                        imageBarriers.emplace_back(barrier.getImageBarrier(eResource));
                    if(eResource.contains<egn::vk::Buffer>())
                        bufferBarriers.emplace_back(barrier.getBufferBarrier(eResource));
                }
            };
            // For each queue for release/acquire operations
            egn::SparseSet<Barriers> queueBarriers;
            for(auto barrier : pass->barriers) {
                if(barrier.src.queueIndex == VK_QUEUE_FAMILY_IGNORED)
                    barrier.src.queueIndex = barrier.dst.queueIndex;

                if(barrier.src.queueIndex != barrier.dst.queueIndex)
                    queueBarriers[barrier.src.queueIndex].emplace(barrier, renderGraph);

                queueBarriers[barrier.dst.queueIndex].emplace(barrier, renderGraph);
            }

            auto passQueue = initRes.queueFamilies.indices.at(pass->queue);
            auto passCB = commandBuffers.at(passQueue)[frameIndex];

            auto queues = queueBarriers.sparse();
            queues.emplace_back(passQueue);
            for(auto queue : queues) {
                if(!usedCommandBuffers.contains(queue)) {
                    auto cb = commandBuffers.at(queue)[frameIndex];

                    usedCommandBuffers.emplace(queue);
                    CHECK_VK_RES(vkResetCommandBuffer(cb, 0));
                    VkCommandBufferBeginInfo cbBI{
                        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                    };
                    CHECK_VK_RES(vkBeginCommandBuffer(cb, &cbBI));
                }
            }

            currentTracyContext = &tracyContexts.at(passQueue).at(frameIndex);
            TracyVkZone(*currentTracyContext, passCB, "Pass render");
            for(auto [queue, barrierss] : queueBarriers) {
                VkDependencyInfo dependencyInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .bufferMemoryBarrierCount = static_cast<uint32_t>(barrierss.bufferBarriers.size()),
                    .pBufferMemoryBarriers = barrierss.bufferBarriers.data(),
                    .imageMemoryBarrierCount = static_cast<uint32_t>(barrierss.imageBarriers.size()),
                    .pImageMemoryBarriers = barrierss.imageBarriers.data() };
                vkCmdPipelineBarrier2(passCB, &dependencyInfo);
            }

            pass->render(passCB);
        }

        // Submit command buffer
        for(auto queue : usedCommandBuffers) {
            auto cb = commandBuffers.at(queue)[frameIndex];
            TracyVkCollect(tracyContexts.at(queue).at(frameIndex), cb);
            TracyVkCollectHost(tracyContexts.at(queue).at(frameIndex));
            vkEndCommandBuffer(cb);

            VkPipelineStageFlags waitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submitInfo{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &presentSemaphores[frameIndex],
                .pWaitDstStageMask = &waitStages,
                .commandBufferCount = 1,
                .pCommandBuffers = &cb,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &renderSemaphores[imageIndex],
            };

            VkQueue physQueue;
            vkGetDeviceQueue(device, queue, 0, &physQueue);
            CHECK_VK_RES(vkQueueSubmit(physQueue, 1, &submitInfo, fences[frameIndex]));
        }
        usedCommandBuffers.clear();

        frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;

        // Present image
        VkPresentInfoKHR presentInfo{ .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderSemaphores[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &swapchain->swapchain,
            .pImageIndices = &imageIndex };
        CHECK_VK_RES(vkQueuePresentKHR(presentQueue, &presentInfo));

        deltatime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start).count() * 1e-9f;
        runtime += deltatime;
        ++frameCount;
        // LOG_INFO("dt {:.2f}ms fps {:.2f}", deltatime * 1e3, 1 / deltatime);
    }

    CHECK_VK_RES(vkDeviceWaitIdle(device));

    ////////////////////////////////////////////////////////////////

    for(auto &q : tracyContexts.dense()) {
        for([[maybe_unused]] auto &ctx : q) {
            TracyVkDestroy(ctx);
        }
    }

    for(uint i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(device, presentSemaphores[i], nullptr);
        vkDestroyFence(device, fences[i], nullptr);
    }
    for(auto &semaphore : renderSemaphores)
        vkDestroySemaphore(device, semaphore, nullptr);

    vkDestroyCommandPool(device, commandPool, nullptr);

    for(auto e : sReg.view<egn::VulkanModel>()) {
        for(auto &mesh : e.get<egn::VulkanModel>().meshes) {
            egn::vk::destroy(mesh.buffers.pos);
            egn::vk::destroy(mesh.buffers.attr);
            egn::vk::destroy(mesh.buffers.idx);
        }
    }
    for(egn::RestrictedEntity<egn::vk::Image> e : sReg.view<egn::vk::Image>()) {
        if(e->owns) {
            LOG_TRACE("= Destroying {} {} (valid: {} owns: {})", e, e->createInfo.name, e->valid(), e->owns);
            egn::vk::destroy(*e);
            e.erase<decltype(e)::Component<0>>();
            if(e.empty())
                e.destroy();
        }
    }
    for(egn::RestrictedEntity<egn::vk::Buffer> e : sReg.view<egn::vk::Buffer>()) {
        if(e->owns) {
            LOG_TRACE("= Destroying {} {} (valid: {} owns: {})", e, e->createInfo.name, e->valid(), e->owns);
            egn::vk::destroy(*e);
            e.erase<decltype(e)::Component<0>>();
            if(e.empty())
                e.destroy();
        }
    }
    for(egn::RestrictedEntity<egn::vk::Pipeline> e : sReg.view<egn::vk::Pipeline>()) {
        LOG_TRACE("= Destroying {}", e);
        egn::vk::destroy(*e);
        e.erase<decltype(e)::Component<0>>();
        if(e.empty())
            e.destroy();
    }
    for(egn::RestrictedEntity<egn::vk::Shader> e : sReg.view<egn::vk::Shader>()) {
        LOG_TRACE("= Destroying {} {}", e, e->createInfo.src);
        egn::vk::destroy(*e);
        e.erase<decltype(e)::Component<0>>();
        if(e.empty())
            e.destroy();
    }
    for(egn::RestrictedEntity<egn::vk::Swapchain> e : sReg.view<egn::vk::Swapchain>()) {
        egn::vk::destroy(*e);
        e.erase<decltype(e)::Component<0>>();
        if(e.empty())
            e.destroy();
    }

    LOG_INFO("Ran for {:.3}s avg deltatime: {:.3}ms ({:.3} fps)", runtime, runtime / frameCount * 1e3, frameCount / runtime);
    std::ofstream("tmp/runtime.txt", std::ios::out | std::ios::app) << getDateTime() << "    rtm " << std::setprecision(4) << runtime << " s    avg "
                                                                    << runtime / frameCount * 1e3 << " ms    " << frameCount / runtime << " fps\n";
    LOG_INFO("Exiting");
    return 0;
}
int main(int argc, char **argv) {
    std::filesystem::create_directory("tmp/");

    time_t t = time(0);
    struct tm *now = localtime(&t);
    char buffer[80];
    strftime(buffer, 80, "%Y%m%d%H%M%S.log", now);
    initLogger("tmp/logs/main_" + std::string(buffer));

    CPPTRACE_TRY { return app(argc, argv); }
    CPPTRACE_CATCH(std::exception const &e) { LOG_ERROR("Exception: {}\n{}", e.what(), cpptrace::from_current_exception().to_string(true)); }
}
