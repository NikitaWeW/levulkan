#include "Renderer.hpp"

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
[[maybe_unused]] static VkQueueFlagBits shaderStageToQueue(VkShaderStageFlagBits stage) {
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
    assert(eImage.valid() && (eImage.contains<Texture2D>()) && "Invalid model!");
    assert(mCommandBuffer && "ResourceAllocator uninitialized! (Make sure to not use the default constructor)");
    if(!eImage.contains<vk::Image>() && eImage.contains<Texture2D>())
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
            default: LOG_WARN("Unknown address mode in {}!", eImage); break;
        }
        vk::ImageCreateInfo ci{
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
            .allocInfo = mAllocInfo,
            .commandBuffer = mCommandBuffer,
            .image = {
                .imageType = VK_IMAGE_TYPE_2D,
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
            },
            .data = image.bitmap.pixels.data(),
            .name = image.path
        };
        eImage.emplace<vk::Image>(vk::makeImage(ci));
        eImage.emplace<ImageIndex>(mProcessedImages.size());
        eImage.emplace<DebugName>(ci.name);
        mProcessedImages.emplace_back(eImage);

        vk::insertImageMemoryBarrier(mCommandBuffer, eImage.get<vk::Image>().image,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, ci.image.dimensions.mipLevels, 0, 1}
        );

        LOG_TRACE("Allocated image e{} \"{}\" {}x{}, {} {} mips of type {} format {} usage {} filter {} address mode {}", 
            eImage.id(), 
            image.path, 
            image.bitmap.size.x, image.bitmap.size.y,
            image.srgb ? "srgb" : "linear",
            image.numMipLevels,
            string_VkImageViewType(eImage.get<vk::Image>().createInfo.image.view.viewType), 
            string_VkFormat(eImage.get<vk::Image>().createInfo.image.format),
            string_VkImageUsageFlags(eImage.get<vk::Image>().createInfo.usage),
            string_VkFilter(ci.image.sampler.minFilter),
            string_VkSamplerAddressMode(ci.image.sampler.addressModeU)
        );
    }
    // TODO: cubemaps

    return eImage.get<ImageIndex>().index;
}
void ResourceAllocator::processModel(Entity eModel) {
    assert(eModel.valid() && eModel.contains<Model>() && "Invalid model!");
    if(eModel.contains<VulkanModel>())
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

enum class ResourceType {Image, Buffer};
static ResourceType getResourceType(RestrictedEntityAny<vk::Image, vk::Buffer> e) {
    return e.contains<vk::Image>() ? ResourceType::Image : ResourceType::Buffer;
}
void DescriptorManager::addResource(RestrictedEntity<vk::Pipeline> pipeline, vk::DescriptorBinding binding, RestrictedEntityAny<vk::Image, vk::Buffer> resource, ResourceDescriptorUpdateInfo info) {
    addResource(pipeline, binding, std::vector<Entity>{resource}, info);
}
void DescriptorManager::addResource(RestrictedEntity<vk::Pipeline> pipeline, vk::DescriptorBinding binding, std::vector<Entity> resources, ResourceDescriptorUpdateInfo info) {
    assert(!resources.empty());
    ResourceType type = getResourceType(resources[0]);
    for(uint i = 1; i < resources.size(); ++i) {
        auto e = resources[i];
        assert(e.valid());
        assert(e.contains<vk::Image>() || e.contains<vk::Buffer>());
        assert(getResourceType(e) == type && "All resources must be the same type (buffer / image)");
    }
    if(type == ResourceType::Image) {
        assert(info.imageLayout != VK_IMAGE_LAYOUT_UNDEFINED && "layout required for image resources");
    }
    
    mPipelineResources[pipeline][binding] = resources;
    mInfos[{pipeline, binding}] = info;
    LOG_TRACE("Adding {} {} resources to binding (set={}, binding={}) to pipeline {}", resources.size(), type == ResourceType::Image ? "image" : "buffer", binding.set, binding.binding, static_cast<Entity const &>(pipeline));

    if(!pipeline.contains<ResourceDirty>())
        pipeline.emplace<ResourceDirty>();
}
void DescriptorManager::erase(Entity pipeline, vk::DescriptorBinding binding) {
    if(!mPipelineResources.contains(pipeline)) {
        LOG_ERROR("Descriptor manager does not contain pipeline {}!", pipeline);
        return;
    }
    auto &resources = mPipelineResources.at(pipeline);
    if(!resources.contains(binding)) {
        LOG_ERROR("Descriptor manager does not contain binding (set={}, binding={}) for pipeline {}!", binding.set, binding.binding, pipeline);
        return;
    }
    resources.erase(binding);
    if(resources.empty())
        mPipelineResources.erase(pipeline);
}
#define LOG_DESC(should, msg, ...) if(static_cast<bool>(should)) { LOG_TRACE(msg, __VA_ARGS__); }
void DescriptorManager::update(uint frame, bool force) {
    bool shouldLog = !sReg.view<ResourceDirty>().empty();
    LOG_DESC(shouldLog, "Updating descriptors for frame {}, force {}", frame, force);
    for(auto &[ePipeline, bindings] : mPipelineResources) {
        assert(ePipeline.valid() && ePipeline.contains<vk::Pipeline>());
        auto &pipeline = ePipeline.get<vk::Pipeline>();

        bool pipelineDirty = force || ePipeline.contains<ResourceDirty>();
        if(pipelineDirty) {
            LOG_DESC(shouldLog, "  Pipeline {} is dirty", ePipeline);
        } else  {
            LOG_DESC(shouldLog, "  Processing pipeline {}", ePipeline);
        }
        std::vector<vk::DescriptorWrite> writes;
        for(auto &[binding, resources] : bindings) {
            assert(!resources.empty());

            LOG_DESC(shouldLog, "    Binding (set={}, binding={})", binding.set, binding.binding);

            bool resourceDirty = false;

            if(!pipelineDirty) {
                for(auto const &res : resources) {
                    if(res.contains<ResourceDirty>()) {
                        resourceDirty = true;
                        LOG_DESC(shouldLog, "    Resource {} at binding (set={}, binding={}) is dirty! (image={}, buffer={})", static_cast<Entity const &>(res), binding.set, binding.binding, res.contains<vk::Image>(), res.contains<vk::Buffer>());
                        break;
                    }
                }
            }
            
            if(pipelineDirty || resourceDirty) {
                std::vector<VkDescriptorImageInfo> imageInfos;
                std::vector<VkDescriptorBufferInfo> bufferInfos;
                imageInfos.reserve(resources.size());
                bufferInfos.reserve(resources.size());

                for(auto res : resources) {
                    if(res.contains<vk::Image>()) {
                        auto const &image = res.get<vk::Image>();
                        VkDescriptorImageInfo imageInfo{
                            .sampler = image.sampler,
                            .imageView = image.view,
                            .imageLayout = mInfos.at({ePipeline, binding}).imageLayout,
                        };
                        imageInfos.emplace_back(imageInfo);
                        LOG_DESC(shouldLog, "      Adding image resource {}", static_cast<Entity const &>(res));
                    } else if(res.contains<vk::Buffer>()) {
                        auto const &buffer = res.get<vk::Buffer>();
                        bufferInfos.emplace_back(VkDescriptorBufferInfo{
                            .buffer = buffer.buffer,
                            .offset = 0,
                            .range = mInfos.at({ePipeline, binding}).bufferSize
                        });
                        LOG_DESC(shouldLog, "      Adding buffer resource {}", static_cast<Entity const &>(res));
                    } else {
                        assert(false && "wtf");
                    }
                }

                writes.emplace_back(vk::DescriptorWrite{
                    .dstSet = binding.set,
                    .dstBinding = binding.binding,
                    .imageInfo = imageInfos,
                    .bufferInfo = bufferInfos
                });
            } 

            if(!writes.empty()) {
                vk::writeDescriptors(pipeline, writes, frame);
            }
            if(ePipeline.contains<ResourceDirty>()) {
                Entity(ePipeline).erase<ResourceDirty>(); // I dont even know man...
            }
        }
    }


    for(auto &[ePipeline, bindings] : mPipelineResources) {
        for(auto &[_, resources] : bindings) {
            for(auto &resource : resources) {
                if(resource.contains<ResourceDirty>()) {
                    resource.erase<ResourceDirty>();
                }
            }
        }
    }
}
