#include "Renderer.hpp"
#include <filesystem>


template<typename T>
static VkFormat getBitmapFormat(Bitmap<T> const& bmp, bool srgb) {
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
    else if constexpr (std::is_same_v<T, uint32_t>)
    {
        switch (bmp.numComponents)
        {
            case 1: return VK_FORMAT_R32_UINT;
            case 2: return VK_FORMAT_R32G32_UINT;
            case 3: return VK_FORMAT_R32G32B32_UINT;
            case 4: return VK_FORMAT_R32G32B32A32_UINT;
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
    else if constexpr (std::is_same_v<T, double>)
    {
        switch (bmp.numComponents)
        {
            case 1: return VK_FORMAT_R64_SFLOAT;
            case 2: return VK_FORMAT_R64G64_SFLOAT;
            case 3: return VK_FORMAT_R64G64B64_SFLOAT;
            case 4: return VK_FORMAT_R64G64B64A64_SFLOAT;
        }
    }

    LOG_ERROR("Unsupported Bitmap format");
    return VK_FORMAT_UNDEFINED;
}
static VkFence createFence(VkDevice dev) {
    VkFence fence = nullptr;
    VkFenceCreateInfo fenceCI{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    CHECK_VK_RES(vkCreateFence(dev, &fenceCI, nullptr, &fence));
    return fence;
}
static VkQueueFlagBits shaderStageToQueue(VkShaderStageFlagBits stage) {
    switch(stage) {
    case VK_SHADER_STAGE_VERTEX_BIT:
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
    case VK_SHADER_STAGE_GEOMETRY_BIT:
    case VK_SHADER_STAGE_FRAGMENT_BIT:
    case VK_SHADER_STAGE_ALL_GRAPHICS:
    case VK_SHADER_STAGE_TASK_BIT_EXT:
    case VK_SHADER_STAGE_MESH_BIT_EXT:
            return VK_QUEUE_GRAPHICS_BIT;

    case VK_SHADER_STAGE_COMPUTE_BIT:
            return VK_QUEUE_COMPUTE_BIT;
            
    case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
    case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:
    case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
    case VK_SHADER_STAGE_MISS_BIT_KHR:
    case VK_SHADER_STAGE_INTERSECTION_BIT_KHR:
    case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
            return VK_QUEUE_COMPUTE_BIT;
            
    default: return static_cast<VkQueueFlagBits>(0);
    }
}

ResourceAllocator::ResourceAllocator(vk::AllocationCreateInfo const &allocInfo, VkCommandPool commandPool, VkQueue queue) {
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
ResourceAllocator::~ResourceAllocator() {
    vkDestroyFence(mAllocInfo.device, mFence, nullptr);
}
uint32_t ResourceAllocator::processImage(Entity eImage) {
    assert(eImage.valid() && (eImage.has<Texture2D>()) && "Invalid model!");
    assert(mCommandBuffer && "ResourceAllocator uninitialized! (Make sure to not use the default constructor)");
    if(!eImage.has<vk::Image>() && eImage.has<Texture2D>())
    {
        auto &image = eImage.get<Texture2D>();

        if(image.bitmap.numComponents == 3)
            LOG_WARN("Making R32G32B32 texture2D \"{}\". Maybe change it to 32 bits or something...", image.path);

        VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        switch(image.addressMode)
        {
            case Texture2D::AddressMode::Repeat:            addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;               break;
            case Texture2D::AddressMode::MirroredRepeat:    addressMode = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;      break;
            case Texture2D::AddressMode::ClampToEdge:       addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;        break;
            case Texture2D::AddressMode::ClampToBorder:     addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;      break;
            case Texture2D::AddressMode::MirrorClampToEdge: addressMode = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE; break;
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
        eImage.emplace<ImageIndex>(mProcessedImages.size());
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

    return eImage.get<ImageIndex>().index;
}
void ResourceAllocator::processModel(Entity eModel) {
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
void ResourceAllocator::begin() {
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    CHECK_VK_RES(vkBeginCommandBuffer(mCommandBuffer, &beginInfo));
}
void ResourceAllocator::end() {
    vkEndCommandBuffer(mCommandBuffer);

    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &mCommandBuffer,
    };
    CHECK_VK_RES(vkQueueSubmit(mQueue, 1, &submitInfo, mFence));
    CHECK_VK_RES(vkWaitForFences(mAllocInfo.device, 1, &mFence, true, UINT64_MAX));
}
RenderManager::RenderManager(vk::AllocationCreateInfo allocInfo, SimpleShaderCreateInfo shaderInfo, REntity<vk::Swapchain> swapchain, VkPhysicalDevice device) {
    mAllocInfo  = allocInfo;
    mSwapchain  = swapchain;
    mShaderInfo = shaderInfo;

    std::vector<VkFormat> depthFormatList{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT };
    for(VkFormat &format : depthFormatList) {
        VkFormatProperties2 formatProperties{ .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2 };
        vkGetPhysicalDeviceFormatProperties2(device, format, &formatProperties);
        if(formatProperties.formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            mDepthFormat = format;
            break;
        }
    }
    if(mDepthFormat == VK_FORMAT_UNDEFINED)
    {
        LOG_ERROR("Failed to pick depth image format!");
        mDepthFormat = depthFormatList.at(0);
    }
}
RenderManager::~RenderManager() {
    for(auto [name, shader] : mShaders) {
        vk::destroy(shader);
    }
}
Entity RenderManager::addColorResource(std::string_view name, glm::uvec2 size) {
    vk::ImageCreateInfo ci{
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .view = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .viewType = VK_IMAGE_VIEW_TYPE_2D
        },
    };

    return addImageResource(name, size, ci);
}
Entity RenderManager::addDepthStencilResource(std::string_view name, glm::uvec2 size, 0}) {
    vk::ImageCreateInfo ci{
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = mDepthFormat,
        .view = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .viewType = VK_IMAGE_VIEW_TYPE_2D
        },
    };

    return addImageResource(name, size, ci);
}
Entity RenderManager::addBufferResource(std::string_view name, uint32_t size, void const *data, VkBufferUsageFlags usage) {
    vk::BufferCreateInfo ci{
        .usage = usage,
        .allocInfo = mAllocInfo,
        .data = data,
        .size = size,
        .name = std::string(name),
    };

    Entity res = sReg.create();

    res.emplace<vk::Buffer>(vk::makeBuffer(ci));
    mRenderGraph.setResource(std::string(name), res);

    return res;
}
void RenderManager::addResource(RAnyEntity<vk::Image, vk::Buffer> eResource) {
    auto name = eResource.has<vk::Image>() ? eResource.get<vk::Image>().name() : eResource.get<vk::Buffer>().name();
    mRenderGraph.setResource(name, eResource);
}

void RenderManager::addPass(SimpleRenderPass const &pass) {
    vk::Shader shader = makeShader(pass.shader);

    if(!shader.valid || shader.binDescriptors.empty()) {
        LOG_ERROR("Failed to compile shader {}", pass.shader);
    }

    auto stage = shader.binDescriptors[0].stage;

    mRenderGraph.addPass(vk::RenderPass{
        .name     = pass.name,
        .reads    = pass.reads,
        .writes   = pass.writes,
        .queue    = shaderStageToQueue(stage),
        .callback = pass.callback,
        .shader   = std::move(shader),
        // TODO: pipeline
    });
}


Entity RenderManager::addImageResource(std::string_view name, glm::uvec2 size, vk::ImageCreateInfo ci) {
    Entity res = sReg.create();

    ci.name = name;
    ci.allocInfo = mAllocInfo;

    if(size == glm::uvec2{0, 0}) {
        res.emplace<ResizeToSwapchain>();
        ci.dimensions = {mSwapchain.get<vk::Swapchain>().createInfo.imageExtent.width, mSwapchain.get<vk::Swapchain>().createInfo.imageExtent.height};
    } else {
        ci.dimensions = {size.x, size.y};
    }

    res.emplace<vk::Image>(vk::makeImage(ci));
    mRenderGraph.setResource(std::string(name), res);

    return res;
}
namespace fs = std::filesystem;
vk::Shader RenderManager::makeShader(std::string_view name) {
    auto src = fs::path(mShaderInfo.srcPrefix)/name;
    if(!fs::exists(src))
        LOG_ERROR("{} doesent exist!", src.string());

    auto extension = src.extension().string();
    vk::ShaderBackend backend = vk::ShaderBackend::NONE;
    if(extension == ".glsl")
        backend = vk::ShaderBackend::GLSL;
    else if(extension == ".slang")
        backend = vk::ShaderBackend::SLANG;
    else if(fs::is_directory(src))
        backend = vk::ShaderBackend::GLSL;

    if(backend == vk::ShaderBackend::NONE)
        LOG_ERROR("Cannot deduce shader backend for {}", src.string());

    vk::ShaderCreateInfo ci{
        .backend           = backend,
        .src               = src.string(),
        .bin               = (fs::path(mShaderInfo.binPrefix)/name).string(),
        .device            = mAllocInfo.device,
        .targetVersion     = mShaderInfo.targetVersion,
        .spirvVersion      = mShaderInfo.spirvVersion,
        .includeDirs       = mShaderInfo.includeDirs,
        .systemIncludeDirs = mShaderInfo.systemIncludeDirs,
        .definitions       = mShaderInfo.definitions,
        .optimization      = mShaderInfo.optimization,
    };

    return vk::makeShader(ci);
}

