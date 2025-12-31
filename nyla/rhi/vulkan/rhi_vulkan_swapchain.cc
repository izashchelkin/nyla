#include <algorithm>
#include <cstdint>
#include <vector>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_texture.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla
{

using namespace rhi_vulkan_internal;

void Rhi::Impl::CreateSwapchain()
{
    VkSwapchainKHR oldSwapchain = m_Swapchain;

    std::array oldSwapchainTextures = m_SwapchainTextures;
    uint32_t oldImagesViewsCount = m_SwapchainTexturesCount;

    static bool logPresentModes = true;
    VkPresentModeKHR presentMode = [this] -> VkPresentModeKHR {
        std::vector<VkPresentModeKHR> presentModes;
        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysDev, m_Surface, &presentModeCount, nullptr);

        presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysDev, m_Surface, &presentModeCount, presentModes.data());

        VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;
        for (VkPresentModeKHR presentMode : presentModes)
        {
            if (logPresentModes)
                NYLA_LOG("Present Mode available: %s", string_VkPresentModeKHR(presentMode));

            bool better;
            switch (presentMode)
            {

            case VK_PRESENT_MODE_FIFO_LATEST_READY_KHR: {
                better = !Any(m_Flags & RhiFlags::VSync);
                break;
            }
            case VK_PRESENT_MODE_IMMEDIATE_KHR: {
                better = !Any(m_Flags & RhiFlags::VSync) && bestMode != VK_PRESENT_MODE_FIFO_LATEST_READY_KHR;
                break;
            }

            default: {
                better = false;
                break;
            }
            }

            if (better)
                bestMode = presentMode;
        }

        if (logPresentModes)
        {
            NYLA_LOG("Chose %s", string_VkPresentModeKHR(bestMode));
        }

        logPresentModes = false;

        return bestMode;
    }();

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysDev, m_Surface, &surfaceCapabilities));

    auto surfaceFormat = [this] -> VkSurfaceFormatKHR {
        uint32_t surfaceFormatCount;
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysDev, m_Surface, &surfaceFormatCount, nullptr));

        std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysDev, m_Surface, &surfaceFormatCount, surfaceFormats.data());

        auto it = std::ranges::find_if(surfaceFormats, [](VkSurfaceFormatKHR surfaceFormat) -> bool {
            return surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
                   surfaceFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
        NYLA_ASSERT(it != surfaceFormats.end());
        return *it;
    }();

    auto surfaceExtent = [this, surfaceCapabilities] -> VkExtent2D {
        if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
            return surfaceCapabilities.currentExtent;

        const PlatformWindowSize windowSize = g_Platform->GetWindowSize(m_Window);
        return VkExtent2D{
            .width = std::clamp(windowSize.width, surfaceCapabilities.minImageExtent.width,
                                surfaceCapabilities.maxImageExtent.width),
            .height = std::clamp(windowSize.height, surfaceCapabilities.minImageExtent.height,
                                 surfaceCapabilities.maxImageExtent.height),
        };
    }();

    NYLA_ASSERT(kRhiMaxNumSwapchainTextures >= surfaceCapabilities.minImageCount);
    uint32_t swapchainMinImageCount = std::min(kRhiMaxNumSwapchainTextures, surfaceCapabilities.minImageCount + 1);
    if (surfaceCapabilities.maxImageCount)
        swapchainMinImageCount = std::min(surfaceCapabilities.maxImageCount, swapchainMinImageCount);

    const VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_Surface,
        .minImageCount = swapchainMinImageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = surfaceExtent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = m_Swapchain,
    };
    VK_CHECK(vkCreateSwapchainKHR(m_Dev, &createInfo, nullptr, &m_Swapchain));

    vkGetSwapchainImagesKHR(m_Dev, m_Swapchain, &m_SwapchainTexturesCount, nullptr);

    NYLA_ASSERT(m_SwapchainTexturesCount <= kRhiMaxNumSwapchainTextures);
    std::array<VkImage, kRhiMaxNumSwapchainTextures> swapchainImages;

    vkGetSwapchainImagesKHR(m_Dev, m_Swapchain, &m_SwapchainTexturesCount, swapchainImages.data());

    for (size_t i = 0; i < m_SwapchainTexturesCount; ++i)
    {
        m_SwapchainTextures[i] = CreateTextureFromSwapchainImage(swapchainImages[i], surfaceFormat, surfaceExtent);

#if 0
        const VkImageCreateInfo depthImageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,

            .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT,
            .extent = VkExtent3D{m_SurfaceExtent.width, m_SurfaceExtent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,

            // const void*              pNext;
            // VkImageCreateFlags       flags;
            // VkImageType              imageType;
            // VkFormat                 format;
            // VkExtent3D               extent;
            // uint32_t                 mipLevels;
            // uint32_t                 arrayLayers;
            // VkSampleCountFlagBits    samples;
            // VkImageTiling            tiling;
            // VkImageUsageFlags        usage;
            // VkSharingMode            sharingMode;
            // uint32_t                 queueFamilyIndexCount;
            // const uint32_t*          pQueueFamilyIndices;
            // VkImageLayout            initialLayout;

        };

        VkImage depthImage;
        VK_CHECK(vkCreateImage(m_Dev, &depthImageCreateInfo, m_Alloc, &depthImage));

        const VkImageViewCreateInfo depthImageImageViewCreateInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = depthImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_D32_SFLOAT_S8_UINT,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        VkImageView depthImageView;
        // VK_CHECK(vkCreateImageView(m_Dev, &depthImageImageViewCreateInfo, m_Alloc, &depthImageView));
#endif
    }

    if (oldSwapchain)
    {
        NYLA_ASSERT(oldImagesViewsCount);
        for (uint32_t i = 0; i < oldImagesViewsCount; ++i)
            DestroySwapchainTexture(oldSwapchainTextures[i]);

        vkDestroySwapchainKHR(m_Dev, oldSwapchain, nullptr);
    }
}

auto Rhi::Impl::CreateTextureFromSwapchainImage(VkImage image, VkSurfaceFormatKHR surfaceFormat,
                                                VkExtent2D surfaceExtent) -> RhiTexture
{
    const VkImageViewCreateInfo imageViewCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = surfaceFormat.format,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    VkImageView imageView;
    VK_CHECK(vkCreateImageView(m_Dev, &imageViewCreateInfo, m_Alloc, &imageView));

    VulkanTextureData textureData{
        .isSwapchain = true,
        .image = image,
        .imageView = imageView,
        .memory = VK_NULL_HANDLE,
        .state = RhiTextureState::Present,
        .format = surfaceFormat.format,
        .extent = {surfaceExtent.width, surfaceExtent.height, 1},
    };

    return m_Textures.Acquire(textureData);
}

void Rhi::Impl::DestroySwapchainTexture(RhiTexture texture)
{
    VulkanTextureData textureData = m_Textures.ReleaseData(texture);
    NYLA_ASSERT(textureData.isSwapchain);

    NYLA_ASSERT(textureData.imageView);
    vkDestroyImageView(m_Dev, textureData.imageView, m_Alloc);

    NYLA_ASSERT(textureData.image);
}

auto Rhi::Impl::GetBackbufferTexture() -> RhiTexture
{
    return m_SwapchainTextures[m_SwapchainTextureIndex];
}

//

auto Rhi::GetBackbufferTexture() -> RhiTexture
{
    return m_Impl->GetBackbufferTexture();
}

} // namespace nyla