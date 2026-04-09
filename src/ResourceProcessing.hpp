#pragma once

#include "vk/vk.hpp"
#include "ECS.hpp"
#include "resource/Resources.hpp"
#include "resource/Loaders.hpp"

struct VulkanModel
{
    struct Mesh 
    {
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
    VmaAllocator alloc = VK_NULL_HANDLE;

    // Processed images
    std::vector<Entity> images;

    // Every processed texture has this component.
    // The index in the #images array
    struct ImageIndex
    {
        uint32_t index = 0;
    };
};

/// @returns the image index
uint32_t processImage(ResourceAllocator &allocator, Entity eImage);
VulkanModel &processModel(ResourceAllocator &allocator, Entity eModel);
Entity loadModel(std::string_view path, ModelLoaderOptions options = {}, std::optional<Material> material = {});
