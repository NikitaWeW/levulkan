/*
$$\    $$\ $$\   $$\   My vulkan abstraction.
$$ |   $$ |$$ | $$  |  Copyright (c) 2026 Nikita Martynau 
$$ |   $$ |$$ |$$  /   https://opensource.org/license/mit 
\$$\  $$  |$$$$$  /    insert git repo url here
 \$$\$$  / $$  $$<     
  \$$$  /  $$ |\$$\    
   \$  /   $$ | \$$\   
    \_/    \__|  \__|  Swapchain creation utility.
*/
#include "vk.hpp"
#include "Logging.hpp"
using namespace vk;

static VkExtent2D chooseExtent(VkSurfaceCapabilitiesKHR capabilities, VkExtent2D size)
{
    if(capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D actualExtent = size;

        actualExtent.width = glm::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = glm::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}
static void chooseSwapSurfaceFormat(Swapchain &swapchain)
{
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(swapchain.allocInfo.physicalDevice, swapchain.allocInfo.surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(swapchain.allocInfo.physicalDevice, swapchain.allocInfo.surface, &formatCount, formats.data());

    for(auto const &format : formats)
    {
        if(format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            swapchain.surfaceFormat = format;
            return;
        }
    }

    LOG_ERROR("Failed to find swap surface format!");
    swapchain.surfaceFormat = formats.size() > 0 ? formats[0] : VkSurfaceFormatKHR{};
}
static void chooseSwapPresentMode(Swapchain &swapchain)
{
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(swapchain.allocInfo.physicalDevice, swapchain.allocInfo.surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(swapchain.allocInfo.physicalDevice, swapchain.allocInfo.surface, &presentModeCount, presentModes.data());

    for(const auto& availablePresentMode : presentModes) {
        if(availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            swapchain.surfacePresentMode = availablePresentMode;
    }

    swapchain.surfacePresentMode =  VK_PRESENT_MODE_FIFO_KHR;
}
static void getImages(Swapchain &swapchain)
{
    for(auto &view : swapchain.imageViews)
        vkDestroyImageView(swapchain.allocInfo.device, view, nullptr);

    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(swapchain.allocInfo.device, swapchain.swapchain, &imageCount, nullptr);
    swapchain.images.resize(imageCount);
    vkGetSwapchainImagesKHR(swapchain.allocInfo.device, swapchain.swapchain, &imageCount, swapchain.images.data());
    swapchain.imageViews.resize(imageCount);
    
    for(size_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapchain.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchain.surfaceFormat.format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        CHK(vkCreateImageView(swapchain.allocInfo.device, &createInfo, nullptr, &swapchain.imageViews[i]));
    }
}

Swapchain vk::makeSwapchain(SwapchainCreateInfo const &ci)
{
    if(!ci.allocInfo.surface)
    {
        LOG_ERROR("No surface provided for swapchain creation!");
        return {};
    }
    Swapchain swapchain;
    swapchain.allocInfo = ci.allocInfo;
    swapchain.sharingMode = ci.sharingMode;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ci.allocInfo.physicalDevice, ci.allocInfo.surface, &swapchain.capabilities);
    
    chooseSwapSurfaceFormat(swapchain); 
    chooseSwapPresentMode(swapchain);
    swapchain.createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = ci.allocInfo.surface,
        .minImageCount = swapchain.capabilities.minImageCount + 1,
        .imageFormat = swapchain.surfaceFormat.format,
        .imageColorSpace = swapchain.surfaceFormat.colorSpace,
        .imageExtent = chooseExtent(swapchain.capabilities, ci.size),
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = swapchain.sharingMode,
        .preTransform = swapchain.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = swapchain.surfacePresentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    CHK(vkCreateSwapchainKHR(swapchain.allocInfo.device, &swapchain.createInfo, nullptr, &swapchain.swapchain));

    getImages(swapchain);

    return swapchain;
}
void vk::resizeSwapchain(Swapchain &swapchain, VkExtent2D size)
{
    swapchain.createInfo.oldSwapchain = swapchain.swapchain;
    swapchain.createInfo.imageExtent = size;

    CHK(vkCreateSwapchainKHR(swapchain.allocInfo.device, &swapchain.createInfo, nullptr, &swapchain.swapchain));
    getImages(swapchain);
    vkDestroySwapchainKHR(swapchain.allocInfo.device, swapchain.createInfo.oldSwapchain, nullptr);
}


void vk::destroy(Swapchain &swapchain)
{
    for(auto &view : swapchain.imageViews)
        vkDestroyImageView(swapchain.allocInfo.device, view, nullptr);

    vkDestroySwapchainKHR(swapchain.allocInfo.device, swapchain.swapchain, nullptr);
}