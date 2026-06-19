/*
$$\    $$\ $$\   $$\   Vulkan helper functionality.
$$ |   $$ |$$ | $$  |  Copyright (c) 2026 Nikita Martynau 
$$ |   $$ |$$ |$$  /   https://opensource.org/license/mit 
\$$\  $$  |$$$$$  /    insert git repo url here
 \$$\$$  / $$  $$<     
  \$$$  /  $$ |\$$\    
   \$  /   $$ | \$$\   Gpu resource manager
    \_/    \__|  \__|  Utilities for image and buffer management.
*/

#pragma once
#include "vulkan.h"
#include <vector>

namespace vk
{

struct AllocationCreateInfo
{
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VmaPool pool = VK_NULL_HANDLE;
    VkMemoryAllocateFlags allocFlags = 0;
    VkMemoryPropertyFlags requiredFlags = 0;
    VkMemoryPropertyFlags preferredFlags = 0;
};

struct BufferCreateInfo
{
    ///
    VkBufferUsageFlags usage = 0;
    VkBufferCreateFlags createFlags = 0;
    AllocationCreateInfo allocInfo;
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    void const *data = nullptr; ///< If not nullptr, appropriate memory flags are added automatically and the data is copied to mapped location. Adds appropriate flags.
    uint32_t size = 0; // In bytes
    bool map = true; ///< Map the buffer to host memory persistently. Adds appropriate flags.
};

/// @brief A buffer data allocated on the gpu
struct Buffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkBufferCreateInfo createInfo;
    VmaAllocationCreateInfo allocationInfo;

    VkDeviceAddress deviceAddress = 0;
    void *mapped = VK_NULL_HANDLE;

    bool valid() const;
};

Buffer makeBuffer(BufferCreateInfo const &ci);
void destroy(Buffer &buffer);

template<typename T>
inline Buffer makeBuffer(VmaAllocator allocator, T const &obj, VkBufferUsageFlags usage)
{
    return makeBuffer(BufferCreateInfo{
        .usage = usage,
        .allocInfo = {
            .allocator = allocator,
        },
        .data = &obj,
        .size = sizeof(T),
    });
}
template<typename T>
inline Buffer makeBuffer(VmaAllocator allocator, std::vector<T> const &vec, VkBufferUsageFlags usage)
{
    return makeBuffer(BufferCreateInfo{
        .usage = usage,
        .allocInfo = {
            .allocator = allocator,
        },
        .data = vec.data(),
        .size = static_cast<uint32_t>(vec.size() * sizeof(T)),
    });
}

/// @brief The image allocated on the gpu
struct Image
{
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;

    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE; ///< A view on the entire texture in the original format.
    VkSampler sampler = VK_NULL_HANDLE;

    /// The transfer buffer.
    /// Free anytime after submitting the command buffer.
    Buffer srcBuffer;
    
    VkImageCreateInfo imageCreateInfo;
    VkSamplerCreateInfo samplerCreateInfo;
    VmaAllocationCreateInfo allocationInfo;
    VkImageType imageType = VK_IMAGE_TYPE_2D;
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    
    VkImageUsageFlags usage = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;

    /// @brief A small helper function that checks if necessary members handles are not null
    bool valid() const; 
};
struct ImageCreateInfo
{
    /// VK_IMAGE_USAGE_TRANSFER_XXX_BIT is added automatically if data is not nullptr.
    /// VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER or VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE will create the sampler.
    VkImageUsageFlags usage = 0; 
    AllocationCreateInfo allocInfo;
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Command buffer to record transfer commands to
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    
    VkFormat format = VK_FORMAT_UNDEFINED;
    struct Dimensions {
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t depth = 1;
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    } dimensions;
    struct Sampler {
        VkSamplerCreateFlags flags = 0;
        VkFilter magFilter = VK_FILTER_NEAREST;
        VkFilter minFilter = VK_FILTER_NEAREST;
        VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        float mipLodBias = 0;
        bool anisotropyEnable = true;
        float maxAnisotropy = 8;
        bool compareEnable = false;
        VkCompareOp compareOp = VK_COMPARE_OP_NEVER;
        float minLod = 0;
        VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        struct {
            float r = 0, g = 0, b = 0, a = 0;
        } customBorderColor;
        bool unnormalizedCoordinates = false;
    } sampler;
    struct View {
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_NONE; ///< Leave at VK_IMAGE_ASPECT_NONE to skip view creation.
        VkImageType imageType = VK_IMAGE_TYPE_2D;
        VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    } view;
    void const *data = nullptr;
};

Image makeImage(ImageCreateInfo const &ci);
void destroy(Image &image);

} // namespace vk
