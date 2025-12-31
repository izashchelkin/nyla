#include "nyla/rhi/rhi_sampler.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

#include <vulkan/vulkan_core.h>

namespace nyla
{

using namespace rhi_vulkan_internal;

namespace
{

auto ConvertFilter(RhiFilter filter) -> VkFilter
{
    switch (filter)
    {
    case RhiFilter::Linear:
        return VK_FILTER_LINEAR;
    case RhiFilter::Nearest:
        return VK_FILTER_NEAREST;
    }
    NYLA_ASSERT(false);
    return {};
}

auto ConvertSamplerAddressMode(RhiSamplerAddressMode addressMode) -> VkSamplerAddressMode
{
    switch (addressMode)
    {
    case RhiSamplerAddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case RhiSamplerAddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    NYLA_ASSERT(false);
    return {};
}

} // namespace

auto Rhi::Impl::CreateSampler(const RhiSamplerDesc &desc) -> RhiSampler
{
    const VkSamplerCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = ConvertFilter(desc.magFilter),
        .minFilter = ConvertFilter(desc.minFilter),
        .mipmapMode = VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = ConvertSamplerAddressMode(desc.addressModeU),
        .addressModeV = ConvertSamplerAddressMode(desc.addressModeV),
        .addressModeW = ConvertSamplerAddressMode(desc.addressModeW),
        .mipLodBias = 0.f,
        .anisotropyEnable = false,
        .maxAnisotropy = 0.f,
        .minLod = 0.f,
        .maxLod = 0.f,
    };

    VulkanSamplerData samplerData;
    VK_CHECK(vkCreateSampler(m_Dev, &createInfo, m_Alloc, &samplerData.sampler));

    return m_Samplers.Acquire(samplerData);
}

void Rhi::Impl::DestroySampler(RhiSampler sampler)
{
    VulkanSamplerData samplerData = m_Samplers.ReleaseData(sampler);
    vkDestroySampler(m_Dev, samplerData.sampler, m_Alloc);
}

//

auto Rhi::CreateSampler(const RhiSamplerDesc &desc) -> RhiSampler
{
    return m_Impl->CreateSampler(desc);
}

void Rhi::DestroySampler(RhiSampler sampler)
{
    m_Impl->DestroySampler(sampler);
}

} // namespace nyla