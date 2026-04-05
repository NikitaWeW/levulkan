/*
$$\    $$\ $$\   $$\   My vulkan abstraction.
$$ |   $$ |$$ | $$  |  Copyright (c) 2026 Nikita Martynau 
$$ |   $$ |$$ |$$  /   https://opensource.org/license/mit 
\$$\  $$  |$$$$$  /    insert git repo url here
 \$$\$$  / $$  $$<     
  \$$$  /  $$ |\$$\    
   \$  /   $$ | \$$\   Gpu resource manager
    \_/    \__|  \__|  Utilities for image and buffer management.
*/
#include "vk.hpp"
#include "Logging.hpp"
using namespace vk;

Image vk::makeImage(ImageCreateInfo const &ci)
{
    if(ci.allocator == nullptr)
    {
        LOG_ERROR("ImageCreateInfo::allocator is null!");
        return {};
    }
    if(ci.usage == 0)
    {
        LOG_ERROR("ImageCreateInfo::usage is not set!");
        return {};
    }
    if(ci.format == VK_FORMAT_UNDEFINED)
    {
        LOG_ERROR("ImageCreateInfo::format is VK_FORMAT_UNDEFINED!");
        return {};
    }

    Image image{
        .allocator = ci.allocator,
        .usage = ci.usage,
        .format = ci.format,
        .dimensions = ci.dimensions,
    };

    return image;
}
Buffer vk::makeBuffer(BufferCreateInfo const &ci)
{
    if(ci.allocator == nullptr)
    {
        LOG_ERROR("BufferCreateInfo::allocator is null!");
        return {};
    }
    if(ci.usage == 0)
    {
        LOG_ERROR("BufferCreateInfo::usage is not set!");
        return {};
    }

    Buffer buffer{
        .allocator = ci.allocator,
        .pool = ci.pool
    };

    buffer.createInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = ci.size,
        .usage = ci.usage,
    };

    buffer.allocationInfo = {
        .flags = ci.allocFlags,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = ci.requiredFlags,
        .preferredFlags = ci.preferredFlags,
        .pool = ci.pool
    };

    vmaCreateBuffer(ci.allocator, &buffer.createInfo, &buffer.allocationInfo, &buffer.buffer, &buffer.allocation, nullptr);

    if(ci.data) {
        buffer.map();
        std::memcpy(buffer.mapped, ci.data, buffer.size);
    } else if(ci.keepMapped) {
        buffer.map();
    }
    if(!ci.keepMapped)
        buffer.unmap();

    return buffer;
}

template<typename T>
VkFormat getVkFormat(Bitmap<T> const& bmp, bool srgb)
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
Image vk::makeTexture(VmaAllocator allocator, Texture const &texture)
{
    if(texture.bitmap.numComponents == 3)
        LOG_WARN("Making R32G32B32 texture \"{}\". Maybe change it to 32 bits or smth...");

    return makeImage({
        .format = getVkFormat(texture.bitmap, texture.srgb),
        .dimensions = {
            .width = texture.bitmap.size.x,
            .height = texture.bitmap.size.y,
            .mipLevels = texture.numMipLevels
        },
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .data = texture.bitmap.pixels.data()
    });
}
Image vk::makeCubemap(VmaAllocator allocator, Cubemap const &cubemap)
{
    // FIXME: How to pass a layered texture
    // https://github.khronos.org/Vulkan-Site/spec/latest/chapters/copies.html#copies-buffers-images-addressing
}

bool vk::Image::valid()
{
    return image != VK_NULL_HANDLE && view != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE;
}
bool vk::Buffer::valid()
{
    return buffer != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE;
}
void vk::Buffer::map()
{
    if(!!valid())
    {
        LOG_WARN("mapping an invalid buffer!");
        return;
    }
    if(mapped)
        return;
    CHK(vmaMapMemory(allocator, allocation, &mapped)); 
}
void vk::Buffer::unmap()
{
    if(!!valid())
    {
        LOG_WARN("unmapping an invalid buffer!");
        return;
    }
    if(!mapped)
        return;
    vmaUnmapMemory(allocator, allocation); 
    mapped = nullptr;
}

void vk::destroy(Image &image)
{

}
void vk::destroy(Buffer &buffer)
{
    vmaDestroyBuffer(buffer.allocator, buffer.buffer, buffer.allocation);
}