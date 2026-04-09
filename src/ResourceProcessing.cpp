#include "ResourceProcessing.hpp"
#include "Logging.hpp"

Entity loadModel(Registry &reg, std::string_view path, ModelLoaderOptions options, std::optional<Material> material)
{
    static ModelLoader loader(reg.getReg());
    
    auto eModel = Entity{&reg, loader.loadFromFile(path, options)};
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

uint32_t processImage(ResourceAllocator &alloc, Entity eImage)
{
    if(!eImage.has<vk::Image>())
    {
        auto &image = eImage.get<Texture>();

        if(image.bitmap.numComponents == 3)
            LOG_WARN("Making R32G32B32 texture \"{}\". Maybe change it to 32 bits or something...");

        vk::ImageCreateInfo ci{
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
            .allocInfo = {
                .allocator = alloc.alloc,
            },
            .format = getBitmapFormat(image.bitmap, image.srgb),
            .dimensions = {
                .width = image.bitmap.size.x,
                .height = image.bitmap.size.y,
                .mipLevels = image.numMipLevels
            },
            .data = image.bitmap.pixels.data()
        };
        eImage.emplace<vk::Image>(vk::makeImage(ci));
        eImage.emplace<ResourceAllocator::ImageIndex>(alloc.images.size());
        alloc.images.emplace_back(eImage);
    }

    return eImage.get<ResourceAllocator::ImageIndex>().index;
}
VulkanModel &processModel(ResourceAllocator &alloc, Entity eModel)
{
    if(!eModel.has<VulkanModel>())
    {
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

            vulkanMesh.textures = {
                .albedo       = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.albedo      }),
                .metallic     = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.metallic    }),
                .roughness    = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.roughness   }),
                .ambient      = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.ambient     }),
                .normal       = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.normal      }),
                .displacement = processImage(alloc, Entity{&eModel.reg(), mesh.material.textures.displacement}),
            };
        }
    }

    return eModel.get<VulkanModel>();
}
