#include <bits/stdc++.h>

#include "vk/vk.hpp"
#include "ECS.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

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
// Helps to form texture arrays
struct ResourceAllocator
{
    VmaAllocator alloc = nullptr;
    VkCommandBuffer commandBuffer = nullptr;
    VkDevice dev = nullptr;

    // Processed images
    std::vector<Entity> images;

    // Every processed texture has this component.
    // The index in the #images array
    struct ImageIndex
    {
        uint32_t index = 0;
    };
};
struct MatrixData {
    glm::mat4 projMat;
    glm::mat4 viewMat;
    glm::mat4 modelMat;
    glm::mat4 normMat;
};

struct UniformBuffer {
    VulkanMaterial uMaterial;
    MatrixData uMatrixData;
};

////////////////////////////////////////////////////////////////

static Transform lookat(glm::vec3 pos, glm::vec3 center)
{
    auto dir = center - pos;
    auto up = glm::abs(glm::dot(dir, {0,1,0})) > 0.999 ? glm::vec3{1,0,0} : glm::vec3{0,1,0};
    return {
        .position = pos,
        .orientation = glm::normalize(glm::quatLookAt(dir, up))
    };
}

////////////////////////////////////////////////////////////////


static std::string printTexture(Entity e)
{
    if(!e.valid() || !e.has<Texture>())
        return fmt::format("e{} -- INVALID", e.entity());
    auto const &texture = e.get<Texture>();
    return fmt::format("e{}, \"{:<30} {}x{}, {:>3}", e.entity(), texture.path + "\",", texture.bitmap.size.x, texture.bitmap.size.y,(texture.srgb ? "srgb" : "not srgb"));
}
static void printModelData(Entity e)
{
    assert(e.valid() && e.has<Model>());
    Model const &model = e.get<Model>();
    LOG_INFO("");
    LOG_INFO("Model: e{}: \"{}\"", e.entity(), model.path);
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
        LOG_INFO("  Albedo:       {}", printTexture({&e.reg(), mesh.material.textures.albedo}));
        LOG_INFO("  Metallic:     {}", printTexture({&e.reg(), mesh.material.textures.metallic}));
        LOG_INFO("  Roughness:    {}", printTexture({&e.reg(), mesh.material.textures.roughness}));
        LOG_INFO("  Ambient:      {}", printTexture({&e.reg(), mesh.material.textures.ambient}));
        LOG_INFO("  Normal:       {}", printTexture({&e.reg(), mesh.material.textures.normal}));
        LOG_INFO("  Displacement: {}", printTexture({&e.reg(), mesh.material.textures.displacement}));
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
static uint32_t processImage(ResourceAllocator &alloc, Entity eImage)
{
    if(!eImage.has<vk::Image>())
    {
        auto &image = eImage.get<Texture>();

        if(image.bitmap.numComponents == 3)
            LOG_WARN("Making R32G32B32 texture \"{}\". Maybe change it to 32 bits or something...", image.path);

        vk::ImageCreateInfo ci{
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
            .allocInfo = {
                .device = alloc.dev,
                .allocator = alloc.alloc,
            },
            .commandBuffer = alloc.commandBuffer,
            .format = getBitmapFormat(image.bitmap, image.srgb),
            .dimensions = {
                .width = image.bitmap.size.x,
                .height = image.bitmap.size.y,
                .mipLevels = image.numMipLevels
            },
            .view = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .imageType = VK_IMAGE_TYPE_2D,
                .viewType = VK_IMAGE_VIEW_TYPE_2D
            },
            .data = image.bitmap.pixels.data(),
        };
        eImage.emplace<vk::Image>(vk::makeImage(ci));
        eImage.emplace<ResourceAllocator::ImageIndex>(alloc.images.size());
        alloc.images.emplace_back(eImage);

        LOG_TRACE("Allocated image e{} {} of type {} format {} usage {}", 
            eImage.entity(), 
            eImage.has<Texture>() ? eImage.get<Texture>().path : "<no path>", 
            string_VkImageViewType(eImage.get<vk::Image>().viewType), 
            string_VkFormat(eImage.get<vk::Image>().format),
            string_VkImageUsageFlags(eImage.get<vk::Image>().usage)
        );
    }
    // TODO: cubemaps

    return eImage.get<ResourceAllocator::ImageIndex>().index;
}
static void processModel(ResourceAllocator &alloc, Entity eModel)
{
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
            .pos  = vk::makeBuffer(alloc.alloc, mesh.geometry.positions, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
            .uv   = vk::makeBuffer(alloc.alloc, mesh.geometry.texCoords, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
            .norm = vk::makeBuffer(alloc.alloc, mesh.geometry.normals,   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
            .tan  = vk::makeBuffer(alloc.alloc, mesh.geometry.tangents,  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
            .idx  = vk::makeBuffer(alloc.alloc, mesh.geometry.indices,   VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
        };

        vulkanMesh.material.textures = {
            .albedo       = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.albedo      }),
            .metallic     = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.metallic    }),
            .roughness    = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.roughness   }),
            .ambient      = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.ambient     }),
            .normal       = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.normal      }),
            .displacement = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.displacement}),
        };
        vulkanMesh.material.properties = mesh.material.properties;
    }
}

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
VkFence createFence(VkDevice dev)
{
    VkFence fence = nullptr;
    VkFenceCreateInfo fenceCI{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    CHK(vkCreateFence(dev, &fenceCI, nullptr, &fence));
    return fence;
}
int main(int argc, char **argv)
{
    initLogger();
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
        }
    };

    vk::enableValidationLayers(initInfo);

    vk::InitResult initRes = vk::init(initInfo);

    if(!initRes.success)
    {
        LOG_ERROR("Failed to init vulkan!");
        return -1;
    }
    auto &device = initRes.device;

    vk::ShaderCreateInfo shaderCI{
        .src = "shaders/basic.glsl",
        .bin = "shaders-bin/basic",
        .device = device,
    };
    vk::Shader shader = vk::makeShader(shaderCI);

    if(!shader.valid)
    {
        LOG_ERROR("Failed to compile shader!");
        return -1;
    }

    vk::Swapchain swapchain = vk::makeSwapchain({
        .allocInfo = {
            .surface = initRes.surface,
            .physicalDevice = initRes.physicalDevice,
            .device = device,
        },
        .size = {window.size.x, window.size.y}
    });

    VkCommandPool commandPool = createCommandPool(initRes.queueFamilies.indices.at(VK_QUEUE_GRAPHICS_BIT), device);
    VkQueue graphicsQueue = initRes.queueFamilies.getQueue(VK_QUEUE_GRAPHICS_BIT);

    ////////////////////////////////////////////////////////////////

    auto suzanne = loadModel("assets/suzanne.glb");

    sReg.create(ModelInstance{suzanne}, lookat({-2, -1, -3}, {0, 0, 0}));
    sReg.create(ModelInstance{suzanne}, lookat({ 0, -1, -3}, {0, 0, 0}));
    sReg.create(ModelInstance{suzanne}, lookat({ 2, -1, -3}, {0, 0, 0}));

    for(auto e : sReg.view<Model>())
        printModelData(e);

    ////////////////////////////////////////////////////////////////

    ResourceAllocator alloc;
    {
        VkCommandBufferAllocateInfo commandBufferAllocInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .commandBufferCount = 1,
        };
        CHK(vkAllocateCommandBuffers(device, &commandBufferAllocInfo, &alloc.commandBuffer));
        VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        CHK(vkBeginCommandBuffer(alloc.commandBuffer, &beginInfo));
    
        alloc.alloc = initRes.vma;
        alloc.dev = initRes.device;

        for(auto eModel : sReg.view<Model>(exclude<VulkanModel>{}))
            processModel(alloc, eModel);

        vkEndCommandBuffer(alloc.commandBuffer);
    
        auto fence = createFence(device);
        VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &alloc.commandBuffer,
        };
        CHK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence));
        CHK(vkWaitForFences(device, 1, &fence, true, UINT64_MAX));

        vkDestroyFence(device, fence, nullptr);
    }

    ////////////////////////////////////////////////////////////////

    vk::ImageCreateInfo depthImageCreateInfo{
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .allocInfo = {
            .device = alloc.dev,
            .allocator = alloc.alloc,
        },
        .dimensions = {
            .width = window.size.x,
            .height = window.size.y,
        },
        .view = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .imageType = VK_IMAGE_TYPE_2D,
            .viewType = VK_IMAGE_VIEW_TYPE_2D
        },
    };
    std::vector<VkFormat> depthFormatList{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT };
    for(VkFormat &format : depthFormatList) {
        VkFormatProperties2 formatProperties{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
        vkGetPhysicalDeviceFormatProperties2(initRes.physicalDevice, format, &formatProperties);
        if(formatProperties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthImageCreateInfo.format = format;
            break;
        }
    }

    if(depthImageCreateInfo.format == VK_FORMAT_UNDEFINED)
    {
        LOG_ERROR("Failed to pick depth image format!");
        depthImageCreateInfo.format = depthFormatList.at(0);
    }

    vk::Image &depthImage = sReg.create(vk::makeImage(depthImageCreateInfo)).get<vk::Image>();

    std::vector<VkDescriptorImageInfo> imageInfos;
    for(auto eImage : alloc.images)
    {
        auto &image = eImage.get<vk::Image>();
        imageInfos.emplace_back(VkDescriptorImageInfo{
            .sampler = image.sampler,
            .imageView = image.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        });
    }
    constexpr VkPipelineColorBlendAttachmentState DEFAULT_BLENDING{
        .blendEnable = true,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    vk::GraphicsPipelineCreateInfo pipelineCI{
        .layout = {
            .descriptorBindingFlags = {
                // Explicitly state that the count is variable, because compiler might optimize it away
                {{.set = 0, .binding = 1}, VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT},
            },
            .descriptorWrites = {
                {{.set = 0, .binding = 1}, vk::PipelineLayoutCreateInfo::DescriptorWrite{
                    .imageInfo = imageInfos
                }},
            },
            .framesInFlight = MAX_FRAMES_IN_FLIGHT,
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
                { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
                { 1, 2, VK_FORMAT_R32G32_SFLOAT, 0 },
                { 2, 1, VK_FORMAT_R32G32B32_SFLOAT, 0 },
                { 3, 3, VK_FORMAT_R32G32B32A32_SFLOAT, 0 },
            },
        },
        .attachments = {
            .color = { swapchain.surfaceFormat.format },
            .depth = depthImage.format,
        },
        .blending = {
            .attachments = { DEFAULT_BLENDING }
        },
    };
    vk::Pipeline pipeline = vk::makePipeline(shader, pipelineCI);


    ////////////////////////////////////////////////////////////////

    std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> commandBuffers;
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> presentSemaphores;
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> fences;
    std::vector<VkSemaphore> renderSemaphores;

    for(uint i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) 
    {
        VkCommandBufferAllocateInfo commandBufferAllocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        CHK(vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, &commandBuffers[i]));

        VkSemaphoreCreateInfo semaphoreCI{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .flags = 0
        };
        vkCreateSemaphore(device, &semaphoreCI, nullptr, &presentSemaphores[i]);

        VkFenceCreateInfo fenceCI{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT
        };

        CHK(vkCreateFence(device, &fenceCI, nullptr, &fences[i]));
    }

    renderSemaphores.resize(swapchain.images.size());
    for(auto &semaphore : renderSemaphores)
    {
        VkSemaphoreCreateInfo semaphoreCI{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };
        vkCreateSemaphore(device, &semaphoreCI, nullptr, &semaphore);
    }

    uint frameIndex = 0;
    uint imageIndex = 0;
    float deltatime = 1e-6;

    Controller::Camera &camera = Controller::createCamera(sReg, {0, 2, 4}, {0, 0, 0}).get<Controller::Camera>();
    Controller cameraController;

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
            vkDeviceWaitIdle(device);

            resizeSwapchain(swapchain, windowExtent);

            vk::destroy(depthImage);
            depthImageCreateInfo.dimensions.width = windowExtent.width;
            depthImageCreateInfo.dimensions.height = windowExtent.height;
            depthImage = vk::makeImage(depthImageCreateInfo);
        }

        // Wait on fence
        CHK(vkWaitForFences(device, 1, &fences[frameIndex], true, UINT64_MAX));
        CHK(vkResetFences(device, 1, &fences[frameIndex]));

        // Acquire next image
        auto imageAcquireRes = vkAcquireNextImageKHR(device, swapchain.swapchain, UINT64_MAX, presentSemaphores[frameIndex], nullptr, &imageIndex);
        if(imageAcquireRes == VK_ERROR_OUT_OF_DATE_KHR)
        {
            shouldResize = true;
            // continue;
        } else if(imageAcquireRes != VK_SUCCESS)
        {
            CHK(imageAcquireRes);
        }

        // Update matrix data
        cameraController.update(sReg, deltatime);
        MatrixData matrixData;
        matrixData.projMat = camera.projMat;
        matrixData.viewMat = camera.viewMat;

        // Record command buffer
        auto cb = commandBuffers[frameIndex];
        CHK(vkResetCommandBuffer(cb, 0));

        VkCommandBufferBeginInfo cbBI{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
        };
        CHK(vkBeginCommandBuffer(cb, &cbBI));

        std::array<VkImageMemoryBarrier2, 2> outputBarriers{
            VkImageMemoryBarrier2{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = 0,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .image = swapchain.images[imageIndex],
                .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
            },
            VkImageMemoryBarrier2{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                .image = depthImage.image,
                .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1, .layerCount = 1 }
            }
        };
        VkDependencyInfo barrierDependencyInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 2,
            .pImageMemoryBarriers = outputBarriers.data()
        };
        vkCmdPipelineBarrier2(cb, &barrierDependencyInfo);

        VkRenderingAttachmentInfo colorAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = swapchain.imageViews[imageIndex],
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue{.color{{ 0.0f, 0.4f, 0.0f, 1.0f }}}
        };
        VkRenderingAttachmentInfo depthAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = depthImage.view,
            .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = {.depthStencil = {1.0f,  0}}
        };

        VkRenderingInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = { 0, 0 },
                .extent = windowExtent,
            },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentInfo,
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

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
        for(auto const &set : pipeline.layout.descSets[frameIndex].dense())
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.layout, 0, 1, &set, 0, nullptr);

        // PushConstants pushConstants{
        //     .matrixDataReference = matrixDataBuffers[frameIndex].deviceAddress
        // };
        // vkCmdPushConstants(
        //     cb,
        //     pipelineLayout,
        //     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        //     0,
        //     sizeof(PushConstants),
        //     &pushConstants
        // );

        for(auto eInstance : sReg.view<ModelInstance>())
        {
            auto const &instance = eInstance.get<ModelInstance>();
            if(!instance.eModel.valid())
            {
                LOG_ERROR("Model instance e{} has invalid eModel {}", eInstance.entity(), instance.eModel.entity());
                continue;
            }
            if(!instance.eModel.has<VulkanModel>())
            {
                LOG_ERROR("Model e{} doesent have VulkanModel component!", instance.eModel.entity());
                continue;
            }

            matrixData.modelMat = {1.0f};
            if(eInstance.has<Transform>())
                matrixData.modelMat = eInstance.get<Transform>().getMat();

            matrixData.normMat = glm::inverse(glm::transpose(matrixData.modelMat));
            std::memcpy(pipeline.getBuffer({0, 0}, frameIndex).mapped, &matrixData, sizeof(MatrixData));

            auto &model = instance.eModel.get<VulkanModel>();
            for(auto &mesh : model.meshes)
            {
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

        VkImageMemoryBarrier2 barrierPresent{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = swapchain.images[imageIndex],
            .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1 }
        };
        VkDependencyInfo barrierPresentDependencyInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrierPresent
        };
        vkCmdPipelineBarrier2(cb, &barrierPresentDependencyInfo);

        vkEndCommandBuffer(cb);

        // Submit command buffer
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
        CHK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, fences[frameIndex]));

        frameIndex =(frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
        
        // Present image
        VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderSemaphores[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &swapchain.swapchain,
            .pImageIndices = &imageIndex
        };
        CHK(vkQueuePresentKHR(presentQueue, &presentInfo));
        deltatime = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - start).count() * 1e-9f;
        // LOG_INFO("dt {:.2f}ms fps {:.2f}", deltatime * 1e3, 1 / deltatime);
    }

    CHK(vkDeviceWaitIdle(device));

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
    {
        vk::destroy(e.get<vk::Image>());
    }
    for(auto e : sReg.view<vk::Buffer>())
        vk::destroy(e.get<vk::Buffer>());

    vk::destroy(pipeline);
    vk::destroy(shader);
    vk::destroy(swapchain);

    vmaDestroyAllocator(initRes.vma);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(initRes.instance, initRes.surface, nullptr);
    vkDestroyDebugUtilsMessengerEXT(initRes.instance, initRes.debugMessenger, nullptr);
    vkDestroyInstance(initRes.instance, nullptr);

    LOG_INFO("Exiting");
}