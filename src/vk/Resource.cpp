#include "Resource.hpp"
#include "Utility.hpp"
#include "Logging.hpp"
#include "libraries/vk_format_utils.h"
using namespace vk;

static VmaAllocationCreateInfo makeAllocInfo(AllocationCreateInfo const &ci)
{
    return {
        .flags = ci.allocFlags,
        .usage = VMA_MEMORY_USAGE_AUTO,
        .requiredFlags = ci.requiredFlags,
        .preferredFlags = ci.preferredFlags,
        .pool = ci.pool
    };
}
static void writeImage(Image &image, ImageCreateInfo const &ci)
{
    image.srcBuffer = vk::makeBuffer(BufferCreateInfo{
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .allocInfo = {
            .allocator = image.allocator,
            .allocFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        },
        .data = ci.data,
        .size = ci.dimensions.width * 
                ci.dimensions.height * 
                ci.dimensions.depth * 
                ci.dimensions.arrayLayers * 
                ci.dimensions.samples * 
                vkuGetFormatInfo(ci.format).texel_block_size,
    });

    VkBufferImageCopy2 bufferCopyRegion = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
        .bufferOffset = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = image.imageCreateInfo.arrayLayers,
        },
        .imageExtent = image.imageCreateInfo.extent
    };
    VkCopyBufferToImageInfo2 copyInfo{
        .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
        .srcBuffer = image.srcBuffer.buffer, 
        .dstImage = image.image, 
        .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
        .regionCount = 1, 
        .pRegions = &bufferCopyRegion
    };

    insertImageMemoryBarrier(ci.commandBuffer, image.image,
        VK_ACCESS_NONE,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_PIPELINE_STAGE_NONE,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, image.imageCreateInfo.mipLevels, 0, 1}
    );
    
    vkCmdCopyBufferToImage2(ci.commandBuffer, &copyInfo);

    insertImageMemoryBarrier(ci.commandBuffer, image.image, 
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    );

    for(uint32_t i = 1; i < image.imageCreateInfo.mipLevels; i++)
    {
        VkImageBlit2 imageBlit{
            .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2,
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel   = i - 1,
                .layerCount = 1,
            },
            .srcOffsets = {
                { 0, 0, 0 },
                { int32_t(image.imageCreateInfo.extent.width >> (i - 1)), int32_t(image.imageCreateInfo.extent.height >> (i - 1)), 1 }
            },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel   = i,
                .layerCount = 1,
            },
            .dstOffsets = {
                { 0, 0, 0 },
                { int32_t(image.imageCreateInfo.extent.width >> i), int32_t(image.imageCreateInfo.extent.height >> i), 1 }
            }
        };
        VkBlitImageInfo2 imageBlitInfo{
            .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
            .srcImage = image.image,
            .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .dstImage = image.image,
            .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .regionCount = 1,
            .pRegions = &imageBlit,
            .filter = VK_FILTER_LINEAR
        };

        vkCmdBlitImage2(ci.commandBuffer, &imageBlitInfo);

        insertImageMemoryBarrier(ci.commandBuffer, image.image, 
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            {VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1}
        );
    }
}
Image vk::makeImage(ImageCreateInfo const &ci)
{
    if(ci.allocInfo.allocator == nullptr)
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
    if(!ci.allocInfo.device)
    {
        LOG_ERROR("ImageCreateInfo::allocInfo::device is null!");
        return {};
    }

    Image image{
        .allocator = ci.allocInfo.allocator,
        .device = ci.allocInfo.device,
        .imageType = ci.view.imageType,
        .viewType = ci.view.viewType,
        .usage = ci.usage,
        .format = ci.format,
    };

    if(ci.data)
        image.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    image.imageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = image.imageType,
        .format = image.format,
        .extent = {
            .width = ci.dimensions.width, 
            .height = ci.dimensions.height, 
            .depth = ci.dimensions.depth 
        },
        .mipLevels = ci.dimensions.mipLevels,
        .arrayLayers = ci.dimensions.arrayLayers,
        .samples = ci.dimensions.samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = image.usage,
        .sharingMode = ci.sharingMode,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    image.allocationInfo = makeAllocInfo(ci.allocInfo);

    CHECK_VK_RES(vmaCreateImage(image.allocator, &image.imageCreateInfo, &image.allocationInfo, &image.image, &image.allocation, nullptr));

    if(ci.data)
        writeImage(image, ci);

    if(image.usage & VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || image.usage & VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
    {
        VkSamplerCustomBorderColorCreateInfoEXT customBorder{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT,
            .customBorderColor = {
                .float32 = {ci.sampler.customBorderColor.r, ci.sampler.customBorderColor.g, ci.sampler.customBorderColor.b, ci.sampler.customBorderColor.a}
            },
            .format = image.format
        };
        image.samplerCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = ci.sampler.borderColor == VK_BORDER_COLOR_FLOAT_CUSTOM_EXT ? &customBorder : nullptr,
            .flags = ci.sampler.flags,
            .magFilter = ci.sampler.magFilter,
            .minFilter = ci.sampler.minFilter,
            .mipmapMode = ci.sampler.mipmapMode,
            .addressModeU = ci.sampler.addressModeU,
            .addressModeV = ci.sampler.addressModeV,
            .addressModeW = ci.sampler.addressModeW,
            .mipLodBias = ci.sampler.mipLodBias,
            .anisotropyEnable = ci.sampler.anisotropyEnable,
            .maxAnisotropy = ci.sampler.maxAnisotropy,
            .compareEnable = ci.sampler.compareEnable,
            .compareOp = ci.sampler.compareOp,
            .minLod = ci.sampler.minLod,
            .maxLod = (float) image.imageCreateInfo.mipLevels,
            .borderColor = ci.sampler.borderColor,
            .unnormalizedCoordinates = ci.sampler.unnormalizedCoordinates,
        };
        CHECK_VK_RES(vkCreateSampler(ci.allocInfo.device, &image.samplerCreateInfo, nullptr, &image.sampler));
    }

    if(ci.view.aspectMask != VK_IMAGE_ASPECT_NONE)
    {
        VkImageViewCreateInfo viewCI{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image.image,
            .viewType = image.viewType,
            .format = image.format,
            .subresourceRange = {
                .aspectMask = ci.view.aspectMask,
                .baseMipLevel = 0,
                .levelCount = image.imageCreateInfo.mipLevels,
                .baseArrayLayer = 0,
                .layerCount = image.imageCreateInfo.arrayLayers,
            }
        };
        CHECK_VK_RES(vkCreateImageView(ci.allocInfo.device, &viewCI, nullptr, &image.view));
    }

    return image;
}
Buffer vk::makeBuffer(BufferCreateInfo const &ci)
{
    if(ci.allocInfo.allocator == nullptr)
    {
        LOG_ERROR("BufferCreateInfo::allocInfo::allocator is null!");
        return {};
    }
    if(ci.usage == 0)
    {
        LOG_ERROR("BufferCreateInfo::usage is not set!");
        return {};
    }

    Buffer buffer{
        .allocator = ci.allocInfo.allocator,
    };

    buffer.createInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = ci.size,
        .usage = ci.usage,
        .sharingMode = ci.sharingMode
    };
    buffer.allocationInfo = makeAllocInfo(ci.allocInfo);

    if(ci.data || ci.map)
        buffer.allocationInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    if(buffer.createInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        buffer.allocationInfo.requiredFlags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    if(buffer.createInfo.size == 0)
    {
        LOG_ERROR("Creating a buffer with size of 0!");
        return buffer;
    }
    CHECK_VK_RES(vmaCreateBuffer(ci.allocInfo.allocator, &buffer.createInfo, &buffer.allocationInfo, &buffer.buffer, &buffer.allocation, nullptr));
    
    if(ci.data || ci.map)
        CHECK_VK_RES(vmaMapMemory(buffer.allocator, buffer.allocation, &buffer.mapped)); 
    if(ci.data)
        std::memcpy(buffer.mapped, ci.data, buffer.createInfo.size);

    return buffer;
}

bool vk::Image::valid() const
{
    bool sampled = usage & VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || usage & VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    if(sampled && !sampler)
        return false;

    return image != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE;
}
bool vk::Buffer::valid() const
{
    return buffer != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE;
}

void vk::destroy(Image &image)
{
    destroy(image.srcBuffer);
    if(!image.valid())
        return;

    vmaDestroyImage(image.allocator, image.image, image.allocation);
    if(image.view)
        vkDestroyImageView(image.device, image.view, nullptr);
    if(image.sampler)
        vkDestroySampler(image.device, image.sampler, nullptr);
    image.image = VK_NULL_HANDLE;
    image.allocation = VK_NULL_HANDLE;
    image.view = VK_NULL_HANDLE;
}
void vk::destroy(Buffer &buffer)
{
    if(!buffer.valid())
        return;
    if(buffer.mapped)
        vmaUnmapMemory(buffer.allocator, buffer.allocation);
    vmaDestroyBuffer(buffer.allocator, buffer.buffer, buffer.allocation);
    buffer.buffer = VK_NULL_HANDLE;
    buffer.allocation = VK_NULL_HANDLE;
}