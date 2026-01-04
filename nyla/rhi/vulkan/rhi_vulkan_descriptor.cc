#include <algorithm>
#include <cstdint>

#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_descriptor.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"
#include "vulkan/vulkan_core.h"

namespace nyla
{

using namespace rhi_vulkan_internal;

namespace
{

auto ConvertVulkanBindingType(RhiBindingType bindingType, RhiDescriptorFlags bindingFlags) -> VkDescriptorType
{
    switch (bindingType)
    {
    case RhiBindingType::UniformBuffer: {
        if (Any(bindingFlags & RhiDescriptorFlags::Dynamic))
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        else
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
    case RhiBindingType::StorageBuffer: {
        if (Any(bindingFlags & RhiDescriptorFlags::Dynamic))
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        else
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    case RhiBindingType::Texture:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case RhiBindingType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
    }
    NYLA_ASSERT(false);
    return static_cast<VkDescriptorType>(0);
}

} // namespace

#if 0

auto Rhi::Impl::CreateDescriptorSetLayout(const RhiDescriptorSetLayoutDesc &desc) -> RhiDescriptorSetLayout
{
    VulkanDescriptorSetLayoutData layoutData{
        .descriptors = desc.descriptors,
    };

    std::ranges::sort(layoutData.descriptors, {}, &RhiDescriptorLayoutDesc::binding);

    InlineVec<VkDescriptorSetLayoutBinding, layoutData.descriptors.max_size()> descriptorLayoutBindings;
    InlineVec<VkDescriptorBindingFlags, layoutData.descriptors.max_size()> descriptorLayoutBindingFlags;

    for (const RhiDescriptorLayoutDesc &bindingDesc : layoutData.descriptors)
    {
        NYLA_ASSERT(bindingDesc.arraySize);

        descriptorLayoutBindings.emplace_back(VkDescriptorSetLayoutBinding{
            .binding = bindingDesc.binding,
            .descriptorType = ConvertVulkanBindingType(bindingDesc.type, bindingDesc.flags),
            .descriptorCount = bindingDesc.arraySize,
            .stageFlags = ConvertShaderStageIntoVkShaderStageFlags(bindingDesc.stageFlags),
        });

        uint32_t flags = 0;
        if (Any(bindingDesc.flags & RhiDescriptorFlags::PartiallyBound))
            flags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
        if (Any(bindingDesc.flags & RhiDescriptorFlags::VariableCount))
            flags |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
        if (Any(bindingDesc.flags & RhiDescriptorFlags::UpdateAfterBind))
            flags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

        descriptorLayoutBindingFlags.emplace_back(flags);
    }

    NYLA_ASSERT(descriptorLayoutBindings.size() == descriptorLayoutBindingFlags.size());
    uint32_t bindingCount = descriptorLayoutBindings.size();

    const VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = bindingCount,
        .pBindingFlags = descriptorLayoutBindingFlags.data(),
    };

    const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &bindingFlagsCreateInfo,
        .bindingCount = bindingCount,
        .pBindings = descriptorLayoutBindings.data(),
    };

    VK_CHECK(vkCreateDescriptorSetLayout(m_Dev, &descriptorSetLayoutCreateInfo, m_Alloc, &layoutData.layout));

    return m_DescriptorSetLayouts.Acquire(layoutData);
}

void Rhi::Impl::DestroyDescriptorSetLayout(RhiDescriptorSetLayout layout)
{
    VkDescriptorSetLayout descriptorSetLayout = m_DescriptorSetLayouts.ReleaseData(layout).layout;
    vkDestroyDescriptorSetLayout(m_Dev, descriptorSetLayout, m_Alloc);
}

auto Rhi::Impl::CreateDescriptorSet(RhiDescriptorSetLayout layout) -> RhiDescriptorSet
{
    const VkDescriptorSetLayout descriptorSetLayout = m_DescriptorSetLayouts.ResolveData(layout).layout;

    const VkDescriptorSetAllocateInfo descriptorSetAllocInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_DescriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptorSetLayout,
    };

    VulkanDescriptorSetData descriptorSetData{
        .layout = layout,
    };
    VK_CHECK(vkAllocateDescriptorSets(m_Dev, &descriptorSetAllocInfo, &descriptorSetData.set));

    return m_DescriptorSets.Acquire(descriptorSetData);
}

void Rhi::Impl::WriteDescriptors(std::span<const RhiDescriptorWriteDesc> writes)
{
    InlineVec<VkWriteDescriptorSet, 128> descriptorWrites;
    InlineVec<VkDescriptorBufferInfo, descriptorWrites.max_size() / 2> bufferInfos;
    InlineVec<VkDescriptorImageInfo, descriptorWrites.max_size() / 2> imageInfos;

    for (const RhiDescriptorWriteDesc write : writes)
    {
        const uint32_t binding = write.binding;

        const VulkanDescriptorSetData descriptorSetData = m_DescriptorSets.ResolveData(write.set);
        const VkDescriptorSet vulkanDescriptorSet = descriptorSetData.set;

        const RhiDescriptorSetLayout descriptorSetLayout = descriptorSetData.layout;
        const VulkanDescriptorSetLayoutData layoutData = m_DescriptorSetLayouts.ResolveData(descriptorSetData.layout);

        const auto &descriptorLayout = [binding, &layoutData] -> const RhiDescriptorLayoutDesc & {
            auto it = std::ranges::lower_bound(layoutData.descriptors, binding, {}, &RhiDescriptorLayoutDesc::binding);
            NYLA_ASSERT(it != layoutData.descriptors.end());
            NYLA_ASSERT(it->binding == binding);
            return *it;
        }();

        NYLA_ASSERT(descriptorLayout.type == write.type);
        const RhiBindingType bindingType = descriptorLayout.type;

        VkWriteDescriptorSet &vulkanSetWrite = descriptorWrites.emplace_back(VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = vulkanDescriptorSet,
            .dstBinding = write.binding,
            .dstArrayElement = write.arrayIndex,
            .descriptorCount = 1, // TODO: support contiguous range writes?
            .descriptorType = ConvertVulkanBindingType(bindingType, descriptorLayout.flags),
        });

        const auto &resourceBinding = write.resourceBinding;

        switch (bindingType)
        {
        case RhiBindingType::UniformBuffer: {
            const VulkanBufferData &bufferData = m_Buffers.ResolveData(resourceBinding.buffer.buffer);

            vulkanSetWrite.pBufferInfo = &bufferInfos.emplace_back(VkDescriptorBufferInfo{
                .buffer = bufferData.buffer,
                .offset = resourceBinding.buffer.offset,
                .range = resourceBinding.buffer.range,
            });
            break;
        }

        case RhiBindingType::Texture: {
            const VulkanTextureViewData &textureViewData =
                m_TextureViews.ResolveData(resourceBinding.texture.textureView);
            const VulkanTextureData &textureData = m_Textures.ResolveData(textureViewData.texture);

            vulkanSetWrite.pImageInfo = &imageInfos.emplace_back(VkDescriptorImageInfo{
                .imageView = textureViewData.imageView,
                .imageLayout = textureData.layout,
            });

            break;
        }

        case RhiBindingType::Sampler: {
            const VulkanSamplerData &samplerData = m_Samplers.ResolveData(resourceBinding.sampler.sampler);

            vulkanSetWrite.pImageInfo = &imageInfos.emplace_back(VkDescriptorImageInfo{
                .sampler = samplerData.sampler,
            });
            break;
        }

        default: {
            NYLA_ASSERT(false);
        }
        }
    }

    vkUpdateDescriptorSets(m_Dev, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
}

void Rhi::Impl::DestroyDescriptorSet(RhiDescriptorSet bindGroup)
{
    VkDescriptorSet descriptorSet = m_DescriptorSets.ReleaseData(bindGroup).set;
    vkFreeDescriptorSets(m_Dev, m_DescriptorPool, 1, &descriptorSet);
}

void Rhi::Impl::CmdBindGraphicsBindGroup(RhiCmdList cmd, uint32_t setIndex, RhiDescriptorSet bindGroup,
                                         std::span<const uint32_t> dynamicOffsets)
{ // TODO: validate dynamic offsets size !!!
}

#endif

void Rhi::Impl::BindDescriptorTables(RhiCmdList cmd)
{
    const VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    const VulkanPipelineData &pipelineData = m_GraphicsPipelines.ResolveData(cmdData.boundGraphicsPipeline);
    const VkDescriptorSet &descriptorSet = m_DescriptorSets.ResolveData(bindGroup).set;

    vkCmdBindDescriptorSets(cmdData.cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.layout, setIndex, 1,
                            &descriptorSet, dynamicOffsets.size(), dynamicOffsets.data());
}

void Rhi::Impl::WriteDescriptorTables()
{
    constexpr uint32_t kMaxDescriptorUpdates = 128;

    InlineVec<VkWriteDescriptorSet, kMaxDescriptorUpdates> descriptorWrites;
    InlineVec<VkDescriptorImageInfo, kMaxDescriptorUpdates> descriptorImageInfos;
    InlineVec<VkDescriptorBufferInfo, kMaxDescriptorUpdates> descriptorBufferInfos;

    { // Constants

        // binding 0: PerFrame (range = 256)
        // binding 1: PerPass (range = 512)
        // binding 2: PerDrawSmall (range = 256)
        // binding 3: PerDrawLarge (range = 1024)
    }

    { // TEXTURES
        for (uint32_t i = 0; i < m_TextureViews.size(); ++i)
        {
            auto &slot = m_TextureViews[i];
            if (!slot.used)
                continue;
            if (slot.data.descriptorWritten)
                continue;

            const VulkanTextureViewData &textureViewData = slot.data;
            const VulkanTextureData &textureData = m_Textures.ResolveData(textureViewData.texture);

            VkWriteDescriptorSet &vulkanSetWrite = descriptorWrites.emplace_back(VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = m_TexturesDescriptorTable.set,
                .dstBinding = 0,
                .dstArrayElement = i,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            });

            vulkanSetWrite.pImageInfo = &descriptorImageInfos.emplace_back(VkDescriptorImageInfo{
                .imageView = textureViewData.imageView,
                .imageLayout = textureData.layout,
            });

            slot.data.descriptorWritten = true;
        }
    }

    { // SAMPLERS
        for (uint32_t i = 0; i < m_Samplers.size(); ++i)
        {
            auto &slot = m_Samplers[i];
            if (!slot.used)
                continue;
            if (slot.data.descriptorWritten)
                continue;

            const VulkanSamplerData &samplerData = slot.data;

            VkWriteDescriptorSet &vulkanSetWrite = descriptorWrites.emplace_back(VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = m_SamplersDescriptorTable.set,
                .dstBinding = 0,
                .dstArrayElement = i,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
            });

            vulkanSetWrite.pImageInfo = &descriptorImageInfos.emplace_back(VkDescriptorImageInfo{
                .sampler = samplerData.sampler,
            });

            slot.data.descriptorWritten = true;
        }
    }

    { // CBVs
        for (uint32_t i = 0; i < m_CBVs.size(); ++i)
        {
            auto &slot = m_CBVs[i];
            if (!slot.used)
                continue;
            if (slot.data.descriptorWritten)
                continue;

            const VulkanBufferViewData &bufferViewData = slot.data;
            const VulkanBufferData &bufferData = m_Buffers.ResolveData(bufferViewData.buffer);

            VkWriteDescriptorSet &vulkanSetWrite = descriptorWrites.emplace_back(VkWriteDescriptorSet{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = m_CBVsDescriptorTable.set,
                .dstBinding = 0,
                .dstArrayElement = i,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            });

            vulkanSetWrite.pBufferInfo = &descriptorBufferInfos.emplace_back(VkDescriptorBufferInfo{
                .buffer = bufferData.buffer,
                .offset = bufferViewData.offset,
                .range = bufferViewData.range,
            });

            slot.data.descriptorWritten = true;
        }
    }

    vkUpdateDescriptorSets(m_Dev, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
}

//

auto Rhi::CreateDescriptorSetLayout(const RhiDescriptorSetLayoutDesc &desc) -> RhiDescriptorSetLayout
{
    return m_Impl->CreateDescriptorSetLayout(desc);
}

void Rhi::DestroyDescriptorSetLayout(RhiDescriptorSetLayout layout)
{
    m_Impl->DestroyDescriptorSetLayout(layout);
}

auto Rhi::CreateDescriptorSet(RhiDescriptorSetLayout layout) -> RhiDescriptorSet
{
    return m_Impl->CreateDescriptorSet(layout);
}

void Rhi::DestroyDescriptorSet(RhiDescriptorSet set)
{
    m_Impl->DestroyDescriptorSet(set);
}

void Rhi::WriteDescriptors(std::span<const RhiDescriptorWriteDesc> writes)
{
    m_Impl->WriteDescriptors(writes);
}

} // namespace nyla