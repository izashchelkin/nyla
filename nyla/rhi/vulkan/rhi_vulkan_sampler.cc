#include "nyla/rhi/rhi_sampler.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

#include <vulkan/vulkan_core.h>

namespace nyla
{

using namespace rhi_vulkan_internal;

auto RhiCreateSampler(RhiSamplerDesc) -> RhiSampler
{
    const VkSamplerCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .flags = 0,
        .magFilter = VkFilter::VK_FILTER_LINEAR,
        .minFilter = VkFilter::VK_FILTER_LINEAR,
        .mipmapMode = VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        .addressModeV = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        .addressModeW = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        .mipLodBias = 0.f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 0.f,
        .compareEnable = VK_FALSE,
        .compareOp = VkCompareOp::VK_COMPARE_OP_ALWAYS,
        .minLod = 0.f,
        .maxLod = 1.f,
        .borderColor = VkBorderColor::VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    VulkanSamplerData samplerData;
    VK_CHECK(vkCreateSampler(vk.dev, &createInfo, vk.alloc, &samplerData.sampler));

    return HandleAcquire(rhiHandles.samplers, samplerData);
}

void RhiDestroySampler(RhiSampler sampler)
{
    VulkanSamplerData samplerData = HandleRelease(rhiHandles.samplers, sampler);
    vkDestroySampler(vk.dev, samplerData.sampler, vk.alloc);
}

} // namespace nyla