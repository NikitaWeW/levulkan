#include <bits/stdc++.h>

#include "vk/vk.hpp"
#include "ECS.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "cpptrace/cpptrace.hpp"
#include "cpptrace/from_current.hpp"

#include "Logging.hpp"
#include "IO.hpp"
#include "Controller.hpp"   
#include "resource/Resources.hpp"
#include "resource/Loaders.hpp"

static Registry sReg;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

struct Transform
{
    glm::vec3 position{0};
    glm::quat orientation{1, 0, 0, 0};
    glm::vec3 scale{1};
    inline glm::mat4 getMat() const {
        return glm::translate(glm::mat4{1.0f}, position) * glm::mat4_cast(orientation) * glm::scale(glm::mat4{1.0f}, scale);
    };
};
struct ModelInstance
{
    Entity eModel;
};

struct VulkanMaterial
{
    ::Material::Properties properties;
    // Texture array indices
    struct Textures
    {
        uint32_t albedo;
        uint32_t metallic;
        uint32_t roughness;
        uint32_t ambient;
        uint32_t normal;
        uint32_t displacement;
    } textures;
};
struct VulkanModel
{
    struct Mesh 
    {
        VulkanMaterial material;
        // TODO: https://www.youtube.com/watch?v=7bSzp-QildA
        struct Buffers
        {
            vk::Buffer pos;
            vk::Buffer uv;
            vk::Buffer norm;
            vk::Buffer tan;
            vk::Buffer idx;
        } buffers;
        size_t indexCount;
        size_t meshIndex;
    };

    // TODO: add animation support

    Entity eModel;
    std::vector<Mesh> meshes;
};
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
struct ResizeToSwapchain {};

////////////////////////////////////////////////////////////////

static std::string printTexture(Entity e)
{
    if(!e.valid() || !e.has<Texture>())
        return fmt::format("e{} -- INVALID", e.id());
    auto const &texture = e.get<Texture>();
    return fmt::format("e{}, \"{:<30} {}x{}, {:>3} {} mips", e.id(), texture.path + "\",", texture.bitmap.size.x, texture.bitmap.size.y,(texture.srgb ? "srgb" : "not srgb"), texture.numMipLevels);
}
[[maybe_unused]] static void printModelData(Entity e)
{
    assert(e.valid() && e.has<Model>());
    Model const &model = e.get<Model>();
    LOG_INFO("");
    LOG_INFO("Model: e{}: \"{}\"", e.id(), model.path);
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
static Transform lookat(glm::vec3 pos, glm::vec3 center)
{
    auto dir = glm::normalize(center - pos);
    auto up = glm::abs(glm::dot(dir, {0,1,0})) > 0.999 ? glm::vec3{1,0,0} : glm::vec3{0,1,0};
    return {
        .position = pos,
        .orientation = glm::normalize(glm::quatLookAt(dir, up))
    };
}
static Entity makeWindow(Registry &reg, std::string_view name)
{
    auto eWindow = reg.create<Window>();
    auto &window = eWindow.get<Window>();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window.handle = glfwCreateWindow(800, 600, name.data(), nullptr, nullptr);
    glfwGetWindowSize(window.handle, reinterpret_cast<int *>(&window.size.x), reinterpret_cast<int *>(&window.size.y));
    if(glfwRawMouseMotionSupported())
        glfwSetInputMode(window.handle, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    io::setCallbacks(window.handle, reg.getReg());

    return eWindow;
}
static VkCommandPool createCommandPool(uint32_t index, VkDevice device)
{
    VkCommandPoolCreateInfo commandPoolCI{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = index
    };
    VkCommandPool commandPool;
    vkCreateCommandPool(device, &commandPoolCI, nullptr, &commandPool);
    return commandPool;
}
static VkFence createFence(VkDevice dev)
{
    VkFence fence = nullptr;
    VkFenceCreateInfo fenceCI{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    CHECK_VK_RES(vkCreateFence(dev, &fenceCI, nullptr, &fence));
    return fence;
}
static void updateUniformBufferDescriptors(vk::RingBuffer const &buffer, vk::Pipeline const &pipeline, uint32_t set = 0, uint32_t desc = 0)
{
    vk::writeDescriptors(pipeline, {vk::DescriptorWrite{
        .dstSet = set,
        .dstBinding = desc,
        .bufferInfo = {VkDescriptorBufferInfo{
            .buffer = buffer.getBuffer().buffer,
            .offset = 0,
            .range  = sizeof(UniformBuffer),
        }}
    }});
}
static void fullscreenPass(vk::RenderPass const &pass, VkCommandBuffer cb, VkRenderingAttachmentInfo attachment, VkExtent2D extent)
{
    VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {
            .offset = { 0, 0 },
            .extent = extent,
        },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
        .pDepthAttachment = nullptr
    };

    vkCmdBeginRendering(cb, &renderingInfo);

    VkViewport vp{
        .x = 0,
        .y = static_cast<float>(extent.height),
        .width = static_cast<float>(extent.width),
        .height = -static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(cb, 0, 1, &vp);
    VkRect2D scissor{ .extent = extent };
    vkCmdSetScissor(cb, 0, 1, &scissor);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipeline.pipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipeline.layout.layout, 0, pass.pipeline.layout.descSets.size(), pass.pipeline.layout.descSets.dense().data(), 0, nullptr);
    
    vkCmdDraw(cb, 3, 1, 0, 0);

    vkCmdEndRendering(cb);
}

////////////////////////////////////////////////////////////////


static Entity loadModel(std::string_view path, ModelLoaderOptions options = {}, std::optional<Material> material = {})
{
    static ModelLoader loader(sReg.getReg());
    
    auto eModel = Entity{&sReg, loader.loadFromFile(path, options)};
    auto &model = eModel.get<Model>();
    
    if(material.has_value())
    {
        auto defaultMaterial = loader.getDefaultMaterial();
        if(material->textures.albedo       == INVALID_ENTITY) material->textures.albedo       = defaultMaterial.textures.albedo;
        if(material->textures.metallic     == INVALID_ENTITY) material->textures.metallic     = defaultMaterial.textures.metallic;
        if(material->textures.roughness    == INVALID_ENTITY) material->textures.roughness    = defaultMaterial.textures.roughness;
        if(material->textures.ambient      == INVALID_ENTITY) material->textures.ambient      = defaultMaterial.textures.ambient;
        if(material->textures.normal       == INVALID_ENTITY) material->textures.normal       = defaultMaterial.textures.normal;
        if(material->textures.displacement == INVALID_ENTITY) material->textures.displacement = defaultMaterial.textures.displacement;

        for(auto &mesh : model.meshes)
            mesh.material = material.value();
    }

    return eModel;
}
template<typename T>
static VkFormat getBitmapFormat(Bitmap<T> const& bmp, bool srgb)
{
    if constexpr (std::is_same_v<T, uint8_t>)
    {
        switch (bmp.numComponents)
        {
            case 1: return srgb ? VK_FORMAT_R8_SRGB       : VK_FORMAT_R8_UNORM;
            case 2: return srgb ? VK_FORMAT_R8G8_SRGB     : VK_FORMAT_R8G8_UNORM;
            case 3: return srgb ? VK_FORMAT_R8G8B8_SRGB   : VK_FORMAT_R8G8B8_UNORM;
            case 4: return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        }
    }
    else if constexpr (std::is_same_v<T, float>)
    {
        switch (bmp.numComponents)
        {
            case 1: return VK_FORMAT_R32_SFLOAT;
            case 2: return VK_FORMAT_R32G32_SFLOAT;
            case 3: return VK_FORMAT_R32G32B32_SFLOAT;
            case 4: return VK_FORMAT_R32G32B32A32_SFLOAT;
        }
    }
    else if constexpr (std::is_same_v<T, uint16_t>)
    {
        switch (bmp.numComponents)
        {
            case 1: return VK_FORMAT_R16_UNORM;
            case 2: return VK_FORMAT_R16G16_UNORM;
            case 3: return VK_FORMAT_R16G16B16_UNORM;
            case 4: return VK_FORMAT_R16G16B16A16_UNORM;
        }
    }

    LOG_ERROR("Unsupported Bitmap format");
    return VK_FORMAT_UNDEFINED;
}

/// @brief Allocates resources on the gpu
/// TODO: extend for render graph
class ResourceAllocator
{
private:
    vk::AllocationCreateInfo mAllocInfo;
    VkCommandBuffer mCommandBuffer = nullptr;
    VkFence mFence = nullptr;
    VkQueue mQueue = nullptr;
    std::vector<Entity> mProcessedImages;
public:
    /// @brief The index in the #images array
    /// Every processed image has this component.
    struct ImageIndex {
        uint32_t index = 0;
    };
    ResourceAllocator() = default;
    inline ResourceAllocator(vk::AllocationCreateInfo const &allocInfo, VkCommandPool commandPool, VkQueue queue) {
        mAllocInfo = allocInfo;

        VkCommandBufferAllocateInfo commandBufferAllocInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .commandBufferCount = 1,
        };
        CHECK_VK_RES(vkAllocateCommandBuffers(mAllocInfo.device, &commandBufferAllocInfo, &mCommandBuffer));
        mFence = createFence(mAllocInfo.device);
        mQueue = queue;
    }
    inline ~ResourceAllocator() {
        vkDestroyFence(mAllocInfo.device, mFence, nullptr);
    }
    inline std::vector<Entity> getProcessedImages() const { return mProcessedImages; }

    inline uint32_t processImage(Entity eImage)
    {
        assert(eImage.valid() && (eImage.has<Texture>()) && "Invalid model!");
        assert(mCommandBuffer && "ResourceAllocator uninitialized! (Make sure to not use the default constructor)");
        if(!eImage.has<vk::Image>() && eImage.has<Texture>())
        {
            auto &image = eImage.get<Texture>();

            if(image.bitmap.numComponents == 3)
                LOG_WARN("Making R32G32B32 texture \"{}\". Maybe change it to 32 bits or something...", image.path);

            VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            switch(image.addressMode)
            {
                case Texture::AddressMode::Repeat:            addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;               break;
                case Texture::AddressMode::MirroredRepeat:    addressMode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;      break;
                case Texture::AddressMode::ClampToEdge:       addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;        break;
                case Texture::AddressMode::ClampToBorder:     addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;      break;
                case Texture::AddressMode::MirrorClampToEdge: addressMode = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE; break;
                default: LOG_WARN("Unknown address mode in e{}!", eImage.id()); break;
            }
            vk::ImageCreateInfo ci{
                .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                .allocInfo = mAllocInfo,
                .imageType = VK_IMAGE_TYPE_2D,
                .commandBuffer = mCommandBuffer,
                .format = getBitmapFormat(image.bitmap, image.srgb),
                .dimensions = {
                    .width = image.bitmap.size.x,
                    .height = image.bitmap.size.y,
                    .mipLevels = image.numMipLevels
                },
                .sampler = {
                    .magFilter = image.linearSampling ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
                    .minFilter = image.linearSampling ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
                    .mipmapMode = image.linearSampling ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR,
                    .addressModeU = addressMode,
                    .addressModeV = addressMode,
                    .addressModeW = addressMode,
                    .anisotropyEnable = image.linearSampling,
                },
                .view = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .viewType = VK_IMAGE_VIEW_TYPE_2D
                },
                .data = image.bitmap.pixels.data(),
                .name = image.path
            };
            eImage.emplace<vk::Image>(vk::makeImage(ci));
            eImage.emplace<ResourceAllocator::ImageIndex>(mProcessedImages.size());
            mProcessedImages.emplace_back(eImage);

            LOG_TRACE("Allocated image e{} \"{}\" {}x{}, {} {} mips of type {} format {} usage {} filter {} address mode {}", 
                eImage.id(), 
                image.path, 
                image.bitmap.size.x, image.bitmap.size.y,
                image.srgb ? "srgb" : "linear",
                image.numMipLevels,
                string_VkImageViewType(eImage.get<vk::Image>().createInfo.view.viewType), 
                string_VkFormat(eImage.get<vk::Image>().createInfo.format),
                string_VkImageUsageFlags(eImage.get<vk::Image>().createInfo.usage),
                string_VkFilter(ci.sampler.minFilter),
                string_VkSamplerAddressMode(ci.sampler.addressModeU)
            );
        }
        // TODO: cubemaps

        return eImage.get<ResourceAllocator::ImageIndex>().index;
    }
    inline void processModel(Entity eModel)
    {
        assert(eModel.valid() && eModel.has<Model>() && "Invalid model!");
        if(eModel.has<VulkanModel>())
            return;

        auto &model = eModel.get<Model>();
        eModel.emplace<VulkanModel>();
        auto &vulkanModel = eModel.get<VulkanModel>();
        for(uint i = 0; i < model.meshes.size(); ++i)
        {
            auto const &mesh = model.meshes[i];
            auto &vulkanMesh = vulkanModel.meshes.emplace_back();
            
            vulkanMesh.meshIndex = i;
            vulkanMesh.indexCount = mesh.geometry.indices.size();

            vulkanMesh.buffers = {
                .pos  = vk::makeBuffer(mAllocInfo.allocator, mesh.geometry.positions, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
                .uv   = vk::makeBuffer(mAllocInfo.allocator, mesh.geometry.texCoords, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
                .norm = vk::makeBuffer(mAllocInfo.allocator, mesh.geometry.normals,   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
                .tan  = vk::makeBuffer(mAllocInfo.allocator, mesh.geometry.tangents,  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
                .idx  = vk::makeBuffer(mAllocInfo.allocator, mesh.geometry.indices,   VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
            };

            vulkanMesh.material.textures = {
                .albedo       = processImage(Entity{&eModel.reg(), mesh.material.textures.albedo      }),
                .metallic     = processImage(Entity{&eModel.reg(), mesh.material.textures.metallic    }),
                .roughness    = processImage(Entity{&eModel.reg(), mesh.material.textures.roughness   }),
                .ambient      = processImage(Entity{&eModel.reg(), mesh.material.textures.ambient     }),
                .normal       = processImage(Entity{&eModel.reg(), mesh.material.textures.normal      }),
                .displacement = processImage(Entity{&eModel.reg(), mesh.material.textures.displacement}),
            };
            vulkanMesh.material.properties = mesh.material.properties;
        }
    }
    inline void begin()
    {
        VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        CHECK_VK_RES(vkBeginCommandBuffer(mCommandBuffer, &beginInfo));
    }
    inline void end()
    {
        vkEndCommandBuffer(mCommandBuffer);
    
        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &mCommandBuffer,
        };
        CHECK_VK_RES(vkQueueSubmit(mQueue, 1, &submitInfo, mFence));
        CHECK_VK_RES(vkWaitForFences(mAllocInfo.device, 1, &mFence, true, UINT64_MAX));
    }
};

////////////////////////////////////////////////////////////////

static bool init()
{
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
int app(int argc, char **argv)
{
    if(!init())
    {
        LOG_ERROR("Failed to init!");
        return -1;
    }

    Window &window = makeWindow(sReg, "levulkan").get<Window>();

    vk::InitInfo initInfo{
        .appName = "levulkan",
        .window = window.handle,
        .version = VK_API_VERSION_1_3,
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

    auto eSwapchain = sReg.create(vk::makeSwapchain({
        .alloc = {
            .device = device,
            .physicalDevice = initRes.physicalDevice,
            .surface = initRes.surface,
        },
        .size = {window.size.x, window.size.y},
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .registry = &sReg
    }));
    vk::Swapchain &swapchain = eSwapchain.get<vk::Swapchain>();

    VkCommandPool commandPool = createCommandPool(initRes.queueFamilies.indices.at(VK_QUEUE_GRAPHICS_BIT), device);
    VkQueue graphicsQueue = initRes.queueFamilies.getQueue(VK_QUEUE_GRAPHICS_BIT);

    vk::AllocationCreateInfo const ALLOCATION_INFO{
        .device = initRes.device,
        .allocator = initRes.vma,
        .sharingMode = SHARING_MODE
    };

    ////////////////////////////////////////////////////////////////

    { // Scene
        auto suzanne = loadModel("assets/suzanne.glb");
        auto cube = loadModel("assets/cube.glb");
        auto cubes = loadModel("assets/deccer_cubes/SM_Deccer_Cubes_Textured_Complex.gltf");
        std::vector<Entity> props{suzanne, cube, suzanne};
        uint numProps = 20;

        for(uint i = 0; i < numProps; ++i)
            sReg.create(ModelInstance{props[i*7%(props.size())]}, lookat({(i-numProps*0.5)*2, -1, -3}, {0, 0, 0}));
    
        sReg.create(ModelInstance{cube}, Transform{.position = {0, -4, 0}}); 
        sReg.create(ModelInstance{cubes}, Transform{.position = {-10, 0, 10}}); 
    }
    
    ////////////////////////////////////////////////////////////////

    // for(auto e : sReg.view<Model>())
    //     printModelData(e);

    ResourceAllocator alloc(ALLOCATION_INFO, commandPool, graphicsQueue);

    alloc.begin();
    for(auto e : sReg.view<Texture>())
        alloc.processImage(e);
    for(auto e : sReg.view<Model>())
        alloc.processModel(e);
    alloc.end();

    ////////////////////////////////////////////////////////////////

    std::vector<VkDescriptorImageInfo> imageInfos;
    for(auto eImage : alloc.getProcessedImages())
    {
        auto &image = eImage.get<vk::Image>();
        imageInfos.emplace_back(VkDescriptorImageInfo{
            .sampler = image.sampler,
            .imageView = image.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        });
    }

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(initRes.physicalDevice, &properties);

    // TODO: Make a uniform buffer class that uses vk::RingBuffer and abstracts stuff a little
    vk::RingBuffer uniformBuffer(vk::BufferCreateInfo{
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .allocInfo = ALLOCATION_INFO,
        .size = static_cast<uint32_t>(64*1e6), // 64MB
        .map = true
    });

    /////////////////////////////////////////////////////////

    vk::ImageCreateInfo DEPTH_ATTACHMENT_CREATE_INFO{
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .allocInfo = ALLOCATION_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .dimensions = {swapchain.createInfo.imageExtent.width, swapchain.createInfo.imageExtent.height},
        .view = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .viewType = VK_IMAGE_VIEW_TYPE_2D
        },
    };
    std::vector<VkFormat> depthFormatList{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT };
    for(VkFormat &format : depthFormatList) {
        VkFormatProperties2 formatProperties{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
        vkGetPhysicalDeviceFormatProperties2(initRes.physicalDevice, format, &formatProperties);
        if(formatProperties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            DEPTH_ATTACHMENT_CREATE_INFO.format = format;
            break;
        }
    }
    if(DEPTH_ATTACHMENT_CREATE_INFO.format == VK_FORMAT_UNDEFINED)
    {
        LOG_ERROR("Failed to pick depth image format!");
        DEPTH_ATTACHMENT_CREATE_INFO.format = depthFormatList.at(0);
    }

    vk::ImageCreateInfo COLOR_ATTACHMENT_CREATE_INFO{
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .allocInfo = ALLOCATION_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .dimensions = {swapchain.createInfo.imageExtent.width, swapchain.createInfo.imageExtent.height},
        .view = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .viewType = VK_IMAGE_VIEW_TYPE_2D
        },
    };
    vk::ResourceTraits constexpr COLOR_ATTACHMENT_TRAITS{
        .imageTraits = {
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        },
        .access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .stages = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
    };
    vk::ResourceTraits constexpr DEPTH_STENCIL_ATTACHMENT_TRAITS{
        .imageTraits = {
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
            .layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        },
        .access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .stages = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
    };
    vk::ResourceTraits constexpr SHADER_SAMPLED_TRAITS{
        .imageTraits = {
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
        .access = VK_ACCESS_2_SHADER_READ_BIT,
        .stages = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
    };
    vk::ResourceTraits constexpr TRANSFER_SRC_TRAITS{
        .imageTraits = {
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        },
        .access = VK_ACCESS_2_TRANSFER_READ_BIT,
        .stages = VK_PIPELINE_STAGE_2_TRANSFER_BIT
    };
    vk::ResourceTraits constexpr TRANSFER_DST_TRAITS{
        .imageTraits = {
            .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        },
        .access = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .stages = VK_PIPELINE_STAGE_2_TRANSFER_BIT
    };
    VkPipelineColorBlendAttachmentState constexpr ALPHA_BLENDING{
        .blendEnable = true,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = 0xFF
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

    ///////////////////////////////////////////////////

    bool uniformBufferRealloc = false;
    bool resizedAttachments = true;
    uint frameIndex = 0;
    uint imageIndex = 0;
    float deltatime = 1e-6;

    auto eCamera = Controller::createCamera(sReg, {0, 2, 4}, {0, 0, 0});
    Controller::Camera &camera = eCamera.get<Controller::Camera>();

    vk::RenderGraph renderGraph({
        .queueFamilies = initRes.queueFamilies,
    });

    //////////////////////////////////////////////
    
    COLOR_ATTACHMENT_CREATE_INFO.name = "gbuffer_albedo";
    renderGraph.setResource("gbuffer_albedo", sReg.create(
        vk::makeImage(COLOR_ATTACHMENT_CREATE_INFO),
        ResizeToSwapchain{}
    ));
    COLOR_ATTACHMENT_CREATE_INFO.name = "gbuffer_position";
    renderGraph.setResource("gbuffer_position", sReg.create(
        vk::makeImage(COLOR_ATTACHMENT_CREATE_INFO),
        ResizeToSwapchain{}
    ));
    COLOR_ATTACHMENT_CREATE_INFO.name = "gbuffer_normal";
    renderGraph.setResource("gbuffer_normal", sReg.create(
        vk::makeImage(COLOR_ATTACHMENT_CREATE_INFO),
        ResizeToSwapchain{}
    ));
    COLOR_ATTACHMENT_CREATE_INFO.name = "gbuffer_pbr";
    renderGraph.setResource("gbuffer_pbr", sReg.create(
        vk::makeImage(COLOR_ATTACHMENT_CREATE_INFO),
        ResizeToSwapchain{}
    ));
    DEPTH_ATTACHMENT_CREATE_INFO.name = "gbuffer_depth";
    renderGraph.setResource("gbuffer_depth", sReg.create(
        vk::makeImage(DEPTH_ATTACHMENT_CREATE_INFO),
        ResizeToSwapchain{}
    ));

    COLOR_ATTACHMENT_CREATE_INFO.name = "main_color";
    COLOR_ATTACHMENT_CREATE_INFO.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    renderGraph.setResource("main_color", sReg.create(
        vk::makeImage(COLOR_ATTACHMENT_CREATE_INFO),
        ResizeToSwapchain{}
    ));
    
    ///////////////////////////////////////////////////

    auto gbuffer_pass = [&](vk::RenderPass const &pass, VkCommandBuffer cb) {
        if(uniformBufferRealloc)
            updateUniformBufferDescriptors(uniformBuffer, pass.pipeline);
        
        // Update matrix data
        UniformBuffer uniformBufferData;
        uniformBufferData.uMatrixData.camera.projMat = camera.projMat;
        uniformBufferData.uMatrixData.camera.viewMat = camera.viewMat;

        std::array<VkRenderingAttachmentInfo, 4> colorAttachmentInfos = {
            VkRenderingAttachmentInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = renderGraph.findResource("gbuffer_albedo").get<vk::Image>().view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue{.color{{ 0.0f, 0.0f, 0.2f, 1.0f }}}
            },
            VkRenderingAttachmentInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = renderGraph.findResource("gbuffer_position").get<vk::Image>().view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue{.color{{ 0.0f, 0.0f, 0.2f, 1.0f }}}
            },
            VkRenderingAttachmentInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = renderGraph.findResource("gbuffer_normal").get<vk::Image>().view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue{.color{{ 0.0f, 0.0f, 0.2f, 1.0f }}}
            },
            VkRenderingAttachmentInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = renderGraph.findResource("gbuffer_pbr").get<vk::Image>().view,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue{.color{{ 0.0f, 0.0f, 0.2f, 1.0f }}}
            },
        };
        VkRenderingAttachmentInfo depthAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = renderGraph.findResource("gbuffer_depth").get<vk::Image>().view,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = {.depthStencil = {1.0f,  0}}
        };

        VkRenderingInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = { 0, 0 },
                .extent = { window.size.x, window.size.y },
            },
            .layerCount = 1,
            .colorAttachmentCount = colorAttachmentInfos.size(),
            .pColorAttachments = colorAttachmentInfos.data(),
            .pDepthAttachment = &depthAttachmentInfo
        };

        vkCmdBeginRendering(cb, &renderingInfo);

        VkViewport vp{
            .x = 0,
            .y = static_cast<float>(window.size.y),
            .width = static_cast<float>(window.size.x),
            .height = -static_cast<float>(window.size.y),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        vkCmdSetViewport(cb, 0, 1, &vp);
        VkRect2D scissor{ .extent{ .width = window.size.x, .height = window.size.y } };
        vkCmdSetScissor(cb, 0, 1, &scissor);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipeline.pipeline);
        // Cannot bind all of them because set 0 has dynamic offset
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipeline.layout.layout, 1, 1, &pass.pipeline.layout.descSets.get(1), 0, nullptr);

        for(auto eInstance : sReg.view<ModelInstance>())
        {
            auto const &instance = eInstance.get<ModelInstance>();
            if(!instance.eModel.valid())
            {
                LOG_ERROR("Model instance e{} has invalid eModel {}", eInstance.id(), instance.eModel.id());
                continue;
            }
            if(!instance.eModel.has<VulkanModel>())
            {
                LOG_ERROR("Model e{} doesent have VulkanModel component!", instance.eModel.id());
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

                uint32_t offset = uniformBuffer.request(sizeof(UniformBuffer), frameIndex, properties.limits.minUniformBufferOffsetAlignment);
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipeline.layout.layout, 0, 1, &pass.pipeline.layout.descSets.get(0), 1, &offset);
                std::memcpy(static_cast<char *>(uniformBuffer.getBuffer().mapped) + offset, &uniformBufferData, sizeof(UniformBuffer));

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
    };
    auto gbuffer_shader = vk::makeShader({
        .src = "shaders/deferred/gbuffer.glsl",
        .bin = "shaders-bin/deferred/gbuffer",
        .device = device,
        .includeDirs = {"shaders"}
    });
    assert(gbuffer_shader.valid);
    vk::Pipeline gbuffer_pipeline = vk::makePipeline(gbuffer_shader, vk::GraphicsPipelineCreateInfo{
        .layout = {
            .descriptorTypeOverride = {
                {{.set = 0, .binding = 0}, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC}
            },
            .unsizedDescriptorSize = {
                {{.set = 1, .binding = 0}, imageInfos.size()}
            },
        },
        .dynamicState = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR },
        .allocator = initRes.vma,
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
            .color = std::vector<VkFormat>(4, COLOR_ATTACHMENT_CREATE_INFO.format),
            .depth = DEPTH_ATTACHMENT_CREATE_INFO.format,
        },
        .depthStencil = {
            .depthTestEnable = true
        },
        .blending = {
            .attachments = std::vector<VkPipelineColorBlendAttachmentState>(4, ALPHA_BLENDING),
        },
    });
    assert(gbuffer_pipeline.valid);
    renderGraph.addPass({
        .name = "G Buffer",
        .writes = {
            {"gbuffer_albedo",   COLOR_ATTACHMENT_TRAITS        }, 
            {"gbuffer_position", COLOR_ATTACHMENT_TRAITS        }, 
            {"gbuffer_normal",   COLOR_ATTACHMENT_TRAITS        }, 
            {"gbuffer_pbr",      COLOR_ATTACHMENT_TRAITS        },
            {"gbuffer_depth",    DEPTH_STENCIL_ATTACHMENT_TRAITS}
        },
        .queue = VK_QUEUE_GRAPHICS_BIT,
        .callback = gbuffer_pass,
        .shader = gbuffer_shader,
        .pipeline = gbuffer_pipeline
    });

    vk::writeDescriptors(gbuffer_pipeline, {vk::DescriptorWrite{
        .dstSet = 1,
        .dstBinding = 0,
        .imageInfo = imageInfos,
    }});
    updateUniformBufferDescriptors(uniformBuffer, gbuffer_pipeline);

    // Uniform buffer written in the pass logic

    ///////////////////////////////////////////////////

    auto lighting_pass = [&](vk::RenderPass const &pass, VkCommandBuffer cb) {
        if(resizedAttachments) {
            vk::writeDescriptors(pass.pipeline, {
                vk::DescriptorWrite{
                    .dstSet = 0,
                    .dstBinding = 0,
                    .imageInfo = {VkDescriptorImageInfo{
                        .sampler     = renderGraph.findResource("gbuffer_albedo").get<vk::Image>().sampler,
                        .imageView   = renderGraph.findResource("gbuffer_albedo").get<vk::Image>().view,
                        .imageLayout = SHADER_SAMPLED_TRAITS.imageTraits.layout,
                    }}
                },
                vk::DescriptorWrite{
                    .dstSet = 0,
                    .dstBinding = 1,
                    .imageInfo = {VkDescriptorImageInfo{
                        .sampler     = renderGraph.findResource("gbuffer_position").get<vk::Image>().sampler,
                        .imageView   = renderGraph.findResource("gbuffer_position").get<vk::Image>().view,
                        .imageLayout = SHADER_SAMPLED_TRAITS.imageTraits.layout,
                    }}
                },
                vk::DescriptorWrite{
                    .dstSet = 0,
                    .dstBinding = 2,
                    .imageInfo = {VkDescriptorImageInfo{
                        .sampler     = renderGraph.findResource("gbuffer_normal").get<vk::Image>().sampler,
                        .imageView   = renderGraph.findResource("gbuffer_normal").get<vk::Image>().view,
                        .imageLayout = SHADER_SAMPLED_TRAITS.imageTraits.layout,
                    }}
                },
                vk::DescriptorWrite{
                    .dstSet = 0,
                    .dstBinding = 3,
                    .imageInfo = {VkDescriptorImageInfo{
                        .sampler     = renderGraph.findResource("gbuffer_pbr").get<vk::Image>().sampler,
                        .imageView   = renderGraph.findResource("gbuffer_pbr").get<vk::Image>().view,
                        .imageLayout = SHADER_SAMPLED_TRAITS.imageTraits.layout,
                    }}
                }
            });
        }

        VkRenderingAttachmentInfo colorAttachmentInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = renderGraph.findResource("main_color").get<vk::Image>().view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue{.color{{ 0.2f, 0.0f, 0.0f, 1.0f }}}
        };
        fullscreenPass(pass, cb, colorAttachmentInfo, {window.size.x, window.size.y});
    };
    auto lighting_shader = vk::makeShader({
        .backend = vk::ShaderBackend::SLANG,
        .src = "shaders/deferred/lighting.slang",
        .bin = "shaders-bin/deferred/lighting",
        .device = device,
        .includeDirs = {"shaders"}
    });
    assert(lighting_shader.valid);
    vk::Pipeline lighting_pipeline = vk::makePipeline(lighting_shader, vk::GraphicsPipelineCreateInfo{
        .layout = {},
        .dynamicState = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR },
        .allocator = initRes.vma,
        .input = {},
        .attachments = {
            .color = std::vector<VkFormat>(1, COLOR_ATTACHMENT_CREATE_INFO.format),
        },
        .depthStencil = {},
        .blending = {
            .attachments = { NO_BLENDING }
        },
    });
    assert(lighting_pipeline.valid);

    renderGraph.addPass({
        .name = "Lighting",
        .reads = {
            {"gbuffer_albedo",   "G Buffer", SHADER_SAMPLED_TRAITS}, 
            {"gbuffer_position", "G Buffer", SHADER_SAMPLED_TRAITS}, 
            {"gbuffer_normal",   "G Buffer", SHADER_SAMPLED_TRAITS}, 
            {"gbuffer_pbr",      "G Buffer", SHADER_SAMPLED_TRAITS}, 
        },
        .writes = {{"main_color", COLOR_ATTACHMENT_TRAITS}},
        .queue = VK_QUEUE_GRAPHICS_BIT,
        .callback = lighting_pass,
        .shader = lighting_shader,
        .pipeline = lighting_pipeline
    });


    ///////////////////////////////////////////////////


    auto swapchain_pass = [&](vk::RenderPass const &pass, VkCommandBuffer cb) {
        auto const &main_color = renderGraph.findResource("main_color").get<vk::Image>();
        auto const &swapchain = renderGraph.findResource("swapchain").get<vk::Image>();
        VkImageBlit2 region{
            .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
            .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .srcOffsets = {{0, 0, 0}, {static_cast<int32_t>(main_color.createInfo.dimensions.width), static_cast<int32_t>(main_color.createInfo.dimensions.height), 1}},
            .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .dstOffsets = {{0, 0, 0}, {static_cast<int32_t>(swapchain.createInfo.dimensions.width), static_cast<int32_t>(swapchain.createInfo.dimensions.height), 1}},
        };
        VkBlitImageInfo2 blit{
            .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
            .srcImage = main_color.image,
            .srcImageLayout = TRANSFER_SRC_TRAITS.imageTraits.layout,
            .dstImage = swapchain.image,
            .dstImageLayout = TRANSFER_DST_TRAITS.imageTraits.layout,
            .regionCount = 1,
            .pRegions = &region,
            .filter = VK_FILTER_NEAREST
        };
        vkCmdBlitImage2(cb, &blit);

        // Prepare for presentation
        vk::insertImageMemoryBarrier(cb, renderGraph.findResource("swapchain").get<vk::Image>().image, 
            TRANSFER_DST_TRAITS.access,
            0,
            TRANSFER_DST_TRAITS.imageTraits.layout,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            TRANSFER_DST_TRAITS.stages,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        );
    };

    renderGraph.addPass({
        .name = "Swapchain blit",
        .reads = {{"main_color", "Lighting", TRANSFER_SRC_TRAITS}},
        .writes = {{"swapchain", TRANSFER_DST_TRAITS}},
        .queue = VK_QUEUE_TRANSFER_BIT,
        .callback = swapchain_pass
    });

    ////////////////////////////////////////////////

    assert(renderGraph.build());
    std::ofstream("RenderGraph.dot", std::ios::trunc) << renderGraph.dumpGraphviz(4);

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

    renderSemaphores.resize(swapchain.images.size());
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

    bool shouldResize = false;
    while(!glfwWindowShouldClose(window.handle))
    {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Poll events
        glfwPollEvents();
        auto prevSize = window.size;
        glfwGetWindowSize(window.handle, reinterpret_cast<int *>(&window.size.x), reinterpret_cast<int *>(&window.size.y));
        VkExtent2D windowExtent = { .width = window.size.x, .height = window.size.y };

        shouldResize = shouldResize || prevSize != window.size;
        
        // Resize swapchain
        if(shouldResize) {
            LOG_WARN("Resizing the viewport to {}x{}", windowExtent.width, windowExtent.height);
            CHECK_VK_RES(vkDeviceWaitIdle(device));

            vk::resizeSwapchain(swapchain, windowExtent);
 
            for(auto e : sReg.view<vk::Image, ResizeToSwapchain>())
            {
                auto &image = e.get<vk::Image>();
                vk::destroy(image);
                image.createInfo.dimensions.width = windowExtent.width;
                image.createInfo.dimensions.height = windowExtent.height;
                image = vk::makeImage(image.createInfo);
            }

            shouldResize = false;
            resizedAttachments = true;
        }
        
        auto &listener = eListener.get<io::EventListener>();
        while(!listener.keyEvents.empty())
        {
            auto event = listener.keyEvents.front();
            listener.keyEvents.pop();

            if(event.key == GLFW_KEY_R && event.action == GLFW_PRESS)
            {
                for(auto &pass : renderGraph.getPassesRange())
                {
                    LOG_INFO("Recompiling \"{}\" from pass \"{}\"", pass.shader.createInfo.src, pass.name);
                    auto newShader = vk::makeShader(pass.shader.createInfo);
                    if(newShader.valid)
                    {
                        CHECK_VK_RES(vkDeviceWaitIdle(device));
                        vk::destroy(pass.shader);
                        vk::destroy(pass.pipeline);
                        pass.shader = newShader;
                        // FIXME: thats horrible
                        switch(pass.pipeline.type)
                        {
                        case vk::Pipeline::Type::GRAPHICS:
                        pass.pipeline = vk::makePipeline(pass.shader, pass.pipeline.createInfo.graphics);
                        break;

                        case vk::Pipeline::Type::COMPUTE:
                        pass.pipeline = vk::makePipeline(pass.shader, pass.pipeline.createInfo.compute);
                        break;

                        case vk::Pipeline::Type::RAYTRACING:
                        pass.pipeline = vk::makePipeline(pass.shader, pass.pipeline.createInfo.raytracing);
                        break;

                        default:
                        assert(false && "unknown pipeline");
                        }
                    }
                }
            }
        }

        cameraController.update(sReg, deltatime);

/////////////////////////////////////////////////////////////////////

        // Wait on fence
        CHECK_VK_RES(vkWaitForFences(device, 1, &fences[frameIndex], true, UINT64_MAX));
        CHECK_VK_RES(vkResetFences(device, 1, &fences[frameIndex]));
        
        uniformBuffer.free(frameIndex);
        uniformBufferRealloc = uniformBuffer.realloc();

        // Acquire next image
        auto imageAcquireRes = vkAcquireNextImageKHR(device, swapchain.swapchain, UINT64_MAX, presentSemaphores[frameIndex], nullptr, &imageIndex);
        if(imageAcquireRes == VK_ERROR_OUT_OF_DATE_KHR)
        {
            LOG_TRACE("VK_ERROR_OUT_OF_DATE_KHR");
            shouldResize = true;
            continue;
        } else if(imageAcquireRes != VK_SUBOPTIMAL_KHR) {
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
        renderGraph.setResource("swapchain", swapchain.images[imageIndex]);

        // Record command buffer
        // FUUUUCK
        for(auto passIndex : renderGraph.getPassStack())
        {
            auto const &pass = renderGraph.getPasses().at(passIndex);
            auto const &barriers = renderGraph.getBarriers().at(passIndex);

            struct Barriers {
                std::vector<VkImageMemoryBarrier2> imageBarriers;
                std::vector<VkBufferMemoryBarrier2> bufferBarriers;
                inline void emplace(vk::Barrier const &barrier, Entity resource) {
                    if(resource.has<vk::Image>())
                        imageBarriers.emplace_back(barrier.getImageBarrier(resource));
                    if(resource.has<vk::Buffer>())
                        bufferBarriers.emplace_back(barrier.getBufferBarrier(resource));
                }
            };
            // For each queue for release/acquire operations
            SparseSet<Barriers> queueBarriers;
            for(auto barrier : barriers)
            {
                if(!renderGraph.getResources().contains(barrier.resourceIndex))
                {
                    LOG_ERROR("Resource for index {} is not set via RenderGraph::setResource!");
                    continue;
                }
                auto resource = renderGraph.getResources().at(barrier.resourceIndex);

                if(barrier.src.queueIndex == VK_QUEUE_FAMILY_IGNORED)
                    barrier.src.queueIndex = barrier.dst.queueIndex;

                if(barrier.src.queueIndex != barrier.dst.queueIndex)
                    queueBarriers[barrier.src.queueIndex].emplace(barrier, resource);
                
                queueBarriers[barrier.dst.queueIndex].emplace(barrier, resource);
            }


            auto queue = initRes.queueFamilies.indices.at(pass.queue);
            
            auto queues = queueBarriers.sparse();
            queues.emplace_back(queue);
            for(auto queue : queues)
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

            pass.callback(pass, commandBuffers.at(queue)[frameIndex]);
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
            .pSwapchains = &swapchain.swapchain,
            .pImageIndices = &imageIndex
        };
        CHECK_VK_RES(vkQueuePresentKHR(presentQueue, &presentInfo));
        deltatime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start).count() * 1e-9f;
        uniformBufferRealloc = false;
        resizedAttachments = false;
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
    for(auto e : sReg.view<vk::Image>())
        vk::destroy(e.get<vk::Image>());
    for(auto e : sReg.view<vk::Buffer>())
        vk::destroy(e.get<vk::Buffer>());
    
    for(auto &pass : renderGraph.getPassesRange())
    {
        vk::destroy(pass.pipeline);
        vk::destroy(pass.shader);
    }

    vk::destroy(swapchain);

    LOG_INFO("Exiting");
    return 0;
}
int main(int argc, char **argv)
{
    initLogger();

    CPPTRACE_TRY {
        return app(argc, argv);
    } CPPTRACE_CATCH(std::exception const &e) {
        LOG_ERROR("Exception: {}\n{}", e.what(), cpptrace::from_current_exception().to_string(true));
    }
}   