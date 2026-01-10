#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/log.h"
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

        const PlatformWindowSize windowSize = g_Platform->WinGetSize();
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

    uint32_t swapchainTexturesCount;
    vkGetSwapchainImagesKHR(m_Dev, m_Swapchain, &swapchainTexturesCount, nullptr);

    NYLA_ASSERT(swapchainTexturesCount <= kRhiMaxNumSwapchainTextures);
    std::array<VkImage, kRhiMaxNumSwapchainTextures> swapchainImages;

    vkGetSwapchainImagesKHR(m_Dev, m_Swapchain, &swapchainTexturesCount, swapchainImages.data());

    for (size_t i = 0; i < swapchainMinImageCount; ++i)
    {
        RhiTexture texture;
        RhiRenderTargetView rtv;

        if (m_SwapchainRTVs.size() > i)
        {
            rtv = m_SwapchainRTVs[i];
            VulkanTextureViewData &rtvData = m_RenderTargetViews.ResolveData(rtv);
            rtvData.format = surfaceFormat.format;

            texture = rtvData.texture;
            VulkanTextureData &textureData = m_Textures.ResolveData(texture);
            textureData.image = swapchainImages[i];
            textureData.state = RhiTextureState::Present;
            textureData.layout = VK_IMAGE_LAYOUT_UNDEFINED;
            textureData.format = surfaceFormat.format;
            textureData.extent = VkExtent3D{surfaceExtent.width, surfaceExtent.height, 1};

            vkDestroyImageView(m_Dev, rtvData.imageView, m_Alloc);

            const VkImageViewCreateInfo imageViewCreateInfo{
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = textureData.image,
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = rtvData.format,
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCreateImageView(m_Dev, &imageViewCreateInfo, m_Alloc, &rtvData.imageView);
        }
        else
        {
            const VulkanTextureData textureData{
                .image = swapchainImages[i],
                .memory = nullptr,
                .state = RhiTextureState::Present,
                .format = surfaceFormat.format,
                .extent = VkExtent3D{surfaceExtent.width, surfaceExtent.height, 1},
            };

            texture = m_Textures.Acquire(textureData);

            rtv = CreateRenderTargetView(RhiRenderTargetViewDesc{
                .texture = texture,
            });

            m_SwapchainRTVs.emplace_back(rtv);
        }
    }

    if (oldSwapchain)
        vkDestroySwapchainKHR(m_Dev, oldSwapchain, m_Alloc);
}

auto Rhi::Impl::GetBackbufferView() -> RhiRenderTargetView
{
    return m_SwapchainRTVs[m_SwapchainTextureIndex];
}

//

auto Rhi::GetBackbufferView() -> RhiRenderTargetView
{
    return m_Impl->GetBackbufferView();
}

void Rhi::TriggerSwapchainRecreate()
{
    m_Impl->TriggerSwapchainRecreate();
}

} // namespace nyla