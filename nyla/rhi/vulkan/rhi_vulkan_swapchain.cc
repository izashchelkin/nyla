#include <algorithm>
#include <cstdint>
#include <limits>
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
        const VulkanTextureData textureData{
            .isSwapchain = true,
            .image = swapchainImages[i],
            .memory = nullptr,
            .state = RhiTextureState::Present,
            .format = surfaceFormat.format,
            .extent = {surfaceExtent.width, surfaceExtent.height, 1},
        };
        RhiTexture texture = m_Textures.Acquire(textureData);
        m_SwapchainTextures[i] = texture;

        const RhiTextureView textureView = CreateTextureView(RhiTextureViewDesc{
            .texture = texture,
        });
        m_Textures.ResolveData(texture).view = textureView;
    }

    if (oldSwapchain)
    {
        NYLA_ASSERT(oldImagesViewsCount);
        for (uint32_t i = 0; i < oldImagesViewsCount; ++i)
        {
            VulkanTextureData textureData = m_Textures.ReleaseData(oldSwapchainTextures[i]);
            NYLA_ASSERT(textureData.isSwapchain);
            NYLA_ASSERT(textureData.image);

            DestroyTextureView(textureData.view);
        };

        vkDestroySwapchainKHR(m_Dev, oldSwapchain, m_Alloc);
    }
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