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

namespace vk {

struct AllocationCreateInfo {
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VmaPool pool = VK_NULL_HANDLE;
    VkMemoryAllocateFlags allocFlags = 0;
    VkMemoryPropertyFlags requiredFlags = 0;
    VkMemoryPropertyFlags preferredFlags = 0;
    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
};

struct BufferCreateInfo {
    VkBufferUsageFlags usage = 0;
    VkBufferCreateFlags createFlags = 0;
    AllocationCreateInfo allocInfo;

    VkDeviceSize size = 0; // In bytes

    void const *data = nullptr; ///< If not nullptr, appropriate memory flags are added automatically and the data is copied to mapped location. Adds appropriate flags.
    bool map = true; ///< Map the buffer to host memory persistently. Adds appropriate flags.
    std::string name = ""; ///< Debug name
};

/// @brief A buffer data allocated on the gpu
struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    
    VmaAllocator allocator = VK_NULL_HANDLE;
    BufferCreateInfo createInfo;
    VkBufferCreateInfo bufferCreateInfo;
    VmaAllocationCreateInfo allocationInfo;

    VkDeviceAddress deviceAddress = 0;
    void *mapped = VK_NULL_HANDLE;

    bool owns = true;

    bool valid() const;
};

Buffer makeBuffer(BufferCreateInfo const &ci);
void destroy(Buffer &buffer);

template<typename T>
inline Buffer makeBuffer(VmaAllocator allocator, T const &obj, VkBufferUsageFlags usage) {
    return makeBuffer(BufferCreateInfo{
        .usage = usage,
        .allocInfo = {
            .allocator = allocator,
        },
        .size = sizeof(T),
        .data = &obj,
    });
}
template<typename T>
inline Buffer makeBuffer(VmaAllocator allocator, std::vector<T> const &vec, VkBufferUsageFlags usage) {
    return makeBuffer(BufferCreateInfo{
        .usage = usage,
        .allocInfo = {
            .allocator = allocator,
        },
        .data = vec.data(),
        .size = static_cast<uint32_t>(vec.size() * sizeof(T)),
    });
}

struct ImageCreateInfo {
    /// VK_IMAGE_USAGE_TRANSFER_XXX_BIT is added automatically if data is not nullptr.
    /// VK_IMAGE_USAGE_SAMPLED_BIT will create the sampler.
    VkImageUsageFlags usage = 0; 
    AllocationCreateInfo allocInfo;
    
    // Command buffer to record transfer commands to
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    
    struct ImageInfo {
        VkImageType imageType = VK_IMAGE_TYPE_2D;
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
            VkSamplerAddressMode addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            VkSamplerAddressMode addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            VkSamplerAddressMode addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
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
            VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
        } view;
    } image;

    void const *data = nullptr;
    std::string name = ""; ///< Debug name

    inline VkImageCreateInfo getImageCreateInfo() const {
        return {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = image.imageType,
            .format = image.format,
            .extent = {
                .width = image.dimensions.width, 
                .height = image.dimensions.height, 
                .depth = image.dimensions.depth 
            },
            .mipLevels = image.dimensions.mipLevels,
            .arrayLayers = image.dimensions.arrayLayers,
            .samples = image.dimensions.samples,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
            .sharingMode = allocInfo.sharingMode,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
    }
    // Doesn't include custom border
    inline VkSamplerCreateInfo getSamplerCreateInfo() const {
        return {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .flags = image.sampler.flags,
            .magFilter = image.sampler.magFilter,
            .minFilter = image.sampler.minFilter,
            .mipmapMode = image.sampler.mipmapMode,
            .addressModeU = image.sampler.addressModeU,
            .addressModeV = image.sampler.addressModeV,
            .addressModeW = image.sampler.addressModeW,
            .mipLodBias = image.sampler.mipLodBias,
            .anisotropyEnable = image.sampler.anisotropyEnable,
            .maxAnisotropy = image.sampler.maxAnisotropy,
            .compareEnable = image.sampler.compareEnable,
            .compareOp = image.sampler.compareOp,
            .minLod = image.sampler.minLod,
            .maxLod = (float) image.dimensions.mipLevels,
            .borderColor = image.sampler.borderColor,
            .unnormalizedCoordinates = image.sampler.unnormalizedCoordinates,
        };
    }
};
/// @brief The image allocated on the gpu
struct Image {
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE; ///< A view on the entire texture in the original format.
    VkSampler sampler = VK_NULL_HANDLE;

    /// The transfer buffer.
    /// Free anytime after submitting the command buffer.
    Buffer srcBuffer;
    
    ImageCreateInfo createInfo;
    VmaAllocationCreateInfo allocationInfo;

    bool owns = true;

    /// @brief A small helper function that checks if necessary members handles are not null
    bool valid() const; 
};

Image makeImage(ImageCreateInfo const &ci);
void destroy(Image &image);

} // namespace vk
