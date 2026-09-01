#include "Init.hpp"
#include "Utility.hpp"
#include "Logging.hpp"
#include "vk/Resource.hpp"
using namespace vk;

static VkExtent2D chooseExtent(VkSurfaceCapabilitiesKHR capabilities, VkExtent2D size) {
    if(capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D actualExtent = size;

        actualExtent.width = glm::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = glm::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }
}
static VkSurfaceFormatKHR chooseSwapSurfaceFormat(SwapchainCreateInfo const ci) {
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(ci.alloc.physicalDevice, ci.alloc.surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(ci.alloc.physicalDevice, ci.alloc.surface, &formatCount, formats.data());

    for(auto const &format : formats)
    {
        if(format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }

    LOG_ERROR("Failed to find swap surface format!");
    return formats.size() > 0 ? formats[0] : VkSurfaceFormatKHR{};
}
static VkPresentModeKHR chooseSwapPresentMode(SwapchainCreateInfo const ci) {
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(ci.alloc.physicalDevice, ci.alloc.surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(ci.alloc.physicalDevice, ci.alloc.surface, &presentModeCount, presentModes.data());

    for(const auto& availablePresentMode : presentModes) {
        if(availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            return availablePresentMode;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}
static void getImages(Swapchain &swapchain, Registry &reg) {
    // FIXME: what the hell is this
    for(auto &view : swapchain.images)
    {
        vkDestroyImageView(swapchain.alloc.device, view.get<vk::Image>().view, nullptr);
        reg.destroy(view);
    }
    swapchain.images.clear();

    uint32_t imageCount = 0;
    std::vector<VkImage> images;
    vkGetSwapchainImagesKHR(swapchain.alloc.device, swapchain.swapchain, &imageCount, nullptr);
    images.resize(imageCount);
    std::vector<VkImageView> imageViews;
    vkGetSwapchainImagesKHR(swapchain.alloc.device, swapchain.swapchain, &imageCount, images.data());
    imageViews.resize(imageCount);
    
    for(size_t i = 0; i < imageCount; i++) {
        
        VkImageViewCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchain.createInfo.imageFormat,
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

        CHECK_VK_RES(vkCreateImageView(swapchain.alloc.device, &createInfo, nullptr, &imageViews[i]));
    }

    for(uint i = 0; i < imageCount; ++i)
    {
        auto eImage = swapchain.images.emplace_back(reg.create(vk::Image{
            .image = images[i],
            .view = imageViews[i],
            .createInfo = {
                .usage = swapchain.createInfo.imageUsage,
                .allocInfo = {
                    .device = swapchain.alloc.device,
                    .sharingMode = swapchain.sharingMode
                },
                .image = {
                    .imageType = VK_IMAGE_TYPE_2D,
                    .format = swapchain.createInfo.imageFormat,
                    .dimensions = {
                        .width = swapchain.createInfo.imageExtent.width,
                        .height = swapchain.createInfo.imageExtent.height,
                    },
                    .view = {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .viewType = VK_IMAGE_VIEW_TYPE_2D
                    },
                },
                .name = fmt::format("swapchain_image_{}", i),
            },
            .owns = false,
        }));
        eImage.emplace<Name>(eImage.get<vk::Image>().createInfo.name);
        VkDebugUtilsObjectNameInfoEXT name_info{
            .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .objectType   = VK_OBJECT_TYPE_IMAGE,
            .objectHandle = (uint64_t) images[i],
            .pObjectName  = eImage.get<vk::Image>().createInfo.name.c_str(),
        };
        vkSetDebugUtilsObjectNameEXT(swapchain.alloc.device, &name_info);
    }
}

Swapchain vk::makeSwapchain(SwapchainCreateInfo const &ci) {
    if(!ci.alloc.surface)
    {
        LOG_ERROR("No surface provided for swapchain creation!");
        return {};
    }
    assert(ci.registry);
    Swapchain swapchain;
    swapchain.alloc = ci.alloc;
    swapchain.sharingMode = ci.sharingMode;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ci.alloc.physicalDevice, ci.alloc.surface, &swapchain.capabilities);
    
    auto format = chooseSwapSurfaceFormat(ci);
    swapchain.createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = ci.alloc.surface,
        .minImageCount = swapchain.capabilities.minImageCount + 1,
        .imageFormat = format.format,
        .imageColorSpace = format.colorSpace,
        .imageExtent = chooseExtent(swapchain.capabilities, ci.size),
        .imageArrayLayers = 1,
        .imageUsage = ci.imageUsage,
        .imageSharingMode = swapchain.sharingMode,
        .preTransform = swapchain.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = chooseSwapPresentMode(ci),
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };
    LOG_TRACE("Making swapchain. Present mode: {}, surface format: {}, colorspace: {}", string_VkPresentModeKHR(swapchain.createInfo.presentMode), string_VkFormat(swapchain.createInfo.imageFormat), string_VkColorSpaceKHR(swapchain.createInfo.imageColorSpace));

    CHECK_VK_RES(vkCreateSwapchainKHR(swapchain.alloc.device, &swapchain.createInfo, nullptr, &swapchain.swapchain));

    getImages(swapchain, *ci.registry);

    return swapchain;
}
void vk::resizeSwapchain(Swapchain &swapchain, VkExtent2D size) {
    swapchain.createInfo.oldSwapchain = swapchain.swapchain;
    swapchain.createInfo.imageExtent = size;

    CHECK_VK_RES(vkCreateSwapchainKHR(swapchain.alloc.device, &swapchain.createInfo, nullptr, &swapchain.swapchain));
    assert(swapchain.images.size() >= 1);
    getImages(swapchain, swapchain.images[0].reg());
    vkDestroySwapchainKHR(swapchain.alloc.device, swapchain.createInfo.oldSwapchain, nullptr);
}


void vk::destroy(Swapchain &swapchain) {
    for(auto &image : swapchain.images)
    {
        vkDestroyImageView(swapchain.alloc.device, image.get<vk::Image>().view, nullptr);
    }

    vkDestroySwapchainKHR(swapchain.alloc.device, swapchain.swapchain, nullptr);
}