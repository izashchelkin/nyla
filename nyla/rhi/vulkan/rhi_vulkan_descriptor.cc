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
    CHECK(false);
    return static_cast<VkDescriptorType>(0);
}

} // namespace

auto RhiCreateDescriptorSetLayout(const RhiDescriptorSetLayoutDesc &desc) -> RhiDescriptorSetLayout
{
    VulkanDescriptorSetLayoutData layoutData{
        .descriptors = desc.descriptors,
    };

    std::ranges::sort(layoutData.descriptors, {}, &RhiDescriptorLayoutDesc::binding);

    InlineVec<VkDescriptorSetLayoutBinding, layoutData.descriptors.max_size()> descriptorLayoutBindings;
    InlineVec<VkDescriptorBindingFlags, layoutData.descriptors.max_size()> descriptorLayoutBindingFlags;

    for (const RhiDescriptorLayoutDesc &bindingDesc : layoutData.descriptors)
    {
        CHECK(bindingDesc.arraySize);

        descriptorLayoutBindings.emplace_back(VkDescriptorSetLayoutBinding{
            .binding = bindingDesc.binding,
            .descriptorType = ConvertVulkanBindingType(bindingDesc.type, bindingDesc.flags),
            .descriptorCount = bindingDesc.arraySize,
            .stageFlags = ConvertRhiShaderStageIntoVkShaderStageFlags(bindingDesc.stageFlags),
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

    CHECK_EQ(descriptorLayoutBindings.size(), descriptorLayoutBindingFlags.size());
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

    VK_CHECK(vkCreateDescriptorSetLayout(vk.dev, &descriptorSetLayoutCreateInfo, vk.alloc, &layoutData.layout));

    return rhiHandles.descriptorSetLayouts.Acquire(layoutData);
}

void RhiDestroyDescriptorSetLayout(RhiDescriptorSetLayout layout)
{
    VkDescriptorSetLayout descriptorSetLayout = rhiHandles.descriptorSetLayouts.ReleaseData(layout).layout;
    vkDestroyDescriptorSetLayout(vk.dev, descriptorSetLayout, vk.alloc);
}

auto RhiCreateDescriptorSet(RhiDescriptorSetLayout layout) -> RhiDescriptorSet
{
    const VkDescriptorSetLayout descriptorSetLayout = rhiHandles.descriptorSetLayouts.ResolveData(layout).layout;

    const VkDescriptorSetAllocateInfo descriptorSetAllocInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk.descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptorSetLayout,
    };

    VulkanDescriptorSetData descriptorSetData{
        .layout = layout,
    };
    VK_CHECK(vkAllocateDescriptorSets(vk.dev, &descriptorSetAllocInfo, &descriptorSetData.set));

    return rhiHandles.descriptorSets.Acquire(descriptorSetData);
}

void RhiWriteDescriptors(std::span<const RhiDescriptorWriteDesc> writes)
{
    InlineVec<VkWriteDescriptorSet, 128> descriptorWrites;
    InlineVec<VkDescriptorBufferInfo, descriptorWrites.max_size() / 2> bufferInfos;
    InlineVec<VkDescriptorImageInfo, descriptorWrites.max_size() / 2> imageInfos;

    for (const RhiDescriptorWriteDesc write : writes)
    {
        const uint32_t binding = write.binding;

        const VulkanDescriptorSetData descriptorSetData = rhiHandles.descriptorSets.ResolveData(write.set);
        const VkDescriptorSet vulkanDescriptorSet = descriptorSetData.set;

        const RhiDescriptorSetLayout descriptorSetLayout = descriptorSetData.layout;
        const VulkanDescriptorSetLayoutData layoutData =
            rhiHandles.descriptorSetLayouts.ResolveData(descriptorSetData.layout);

        const auto &descriptorLayout = [binding, &layoutData] -> const RhiDescriptorLayoutDesc & {
            auto it = std::ranges::lower_bound(layoutData.descriptors, binding, {}, &RhiDescriptorLayoutDesc::binding);
            CHECK_NE(it, layoutData.descriptors.end());
            CHECK_EQ(it->binding, binding);
            return *it;
        }();

        CHECK(descriptorLayout.type == write.type);
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
            const VulkanBufferData &bufferData = rhiHandles.buffers.ResolveData(resourceBinding.buffer.buffer);

            vulkanSetWrite.pBufferInfo = &bufferInfos.emplace_back(VkDescriptorBufferInfo{
                .buffer = bufferData.buffer,
                .offset = resourceBinding.buffer.offset,
                .range = resourceBinding.buffer.range,
            });
            break;
        }

        case RhiBindingType::Texture: {
            const VulkanTextureData &textureData = rhiHandles.textures.ResolveData(resourceBinding.texture.texture);

            vulkanSetWrite.pImageInfo = &imageInfos.emplace_back(VkDescriptorImageInfo{
                .imageView = textureData.imageView,
                .imageLayout = textureData.layout,
            });

            break;
        }

        case RhiBindingType::Sampler: {
            const VulkanSamplerData &samplerData = rhiHandles.samplers.ResolveData(resourceBinding.sampler.sampler);

            vulkanSetWrite.pImageInfo = &imageInfos.emplace_back(VkDescriptorImageInfo{
                .sampler = samplerData.sampler,
            });
            break;
        }

        default: {
            CHECK(false);
        }
        }
    }

    vkUpdateDescriptorSets(vk.dev, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
}

void RhiDestroyDescriptorSet(RhiDescriptorSet bindGroup)
{
    VkDescriptorSet descriptorSet = rhiHandles.descriptorSets.ReleaseData(bindGroup).set;
    vkFreeDescriptorSets(vk.dev, vk.descriptorPool, 1, &descriptorSet);
}

void RhiCmdBindGraphicsBindGroup(RhiCmdList cmd, uint32_t setIndex, RhiDescriptorSet bindGroup,
                                 std::span<const uint32_t> dynamicOffsets)
{ // TODO: validate dynamic offsets size !!!
    const VulkanCmdListData &cmdData = rhiHandles.cmdLists.ResolveData(cmd);
    const VulkanPipelineData &pipelineData = rhiHandles.graphicsPipelines.ResolveData(cmdData.boundGraphicsPipeline);
    const VkDescriptorSet &descriptorSet = rhiHandles.descriptorSets.ResolveData(bindGroup).set;

    vkCmdBindDescriptorSets(cmdData.cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.layout, setIndex, 1,
                            &descriptorSet, dynamicOffsets.size(), dynamicOffsets.data());
}

} // namespace nyla