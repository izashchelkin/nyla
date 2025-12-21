#include <cstdint>
#include <cstdlib>

#include "nyla/rhi/rhi_bind_groups.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"
#include "vulkan/vulkan_core.h"

namespace nyla
{

using namespace rhi_vulkan_internal;

namespace
{

auto ConvertVulkanBindingType(RhiBindingType bindingType, RhiBindingFlags bindingFlags) -> VkDescriptorType
{
    switch (bindingType)
    {
    case RhiBindingType::UniformBuffer: {
        if (Any(bindingFlags & RhiBindingFlags::Dynamic))
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        else
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
    case RhiBindingType::StorageBuffer: {
        if (Any(bindingFlags & RhiBindingFlags::Dynamic))
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

auto RhiCreateBindGroupLayout(const RhiBindGroupLayoutDesc &desc) -> RhiBindGroupLayout
{
    CHECK_LE(desc.bindingCount, std::size(desc.bindings));
    std::array<VkDescriptorSetLayoutBinding, std::size(desc.bindings)> descriptorSetLayoutBindings;

    for (uint32_t ibinding = 0; ibinding < desc.bindingCount; ++ibinding)
    {
        const RhiBindingDesc &binding = desc.bindings[ibinding];
        descriptorSetLayoutBindings[ibinding] = {
            .binding = binding.binding,
            .descriptorType = ConvertVulkanBindingType(binding.type, binding.flags),
            .descriptorCount = binding.arraySize,
            .stageFlags = ConvertRhiShaderStageIntoVkShaderStageFlags(binding.stageFlags),
        };
    }

    const VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = desc.bindingCount,
        .pBindings = descriptorSetLayoutBindings.data(),
    };

    VkDescriptorSetLayout descriptorSetLayout;
    VK_CHECK(vkCreateDescriptorSetLayout(vk.dev, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout));

    return HandleAcquire(rhiHandles.bindGroupLayouts, VulkanBindGroupLayoutData{
                                                          .layout = descriptorSetLayout,
                                                          .desc = desc,
                                                      });
}

void RhiDestroyBindGroupLayout(RhiBindGroupLayout layout)
{
    VkDescriptorSetLayout descriptorSetLayout = HandleRelease(rhiHandles.bindGroupLayouts, layout).layout;
    vkDestroyDescriptorSetLayout(vk.dev, descriptorSetLayout, nullptr);
}

auto RhiCreateBindGroup(const RhiBindGroupDesc &desc) -> RhiBindGroup
{
    const VulkanBindGroupLayoutData &layoutData = HandleGetData(rhiHandles.bindGroupLayouts, desc.layout);
    const VkDescriptorSetLayout descriptorSetLayout = layoutData.layout;

    const VkDescriptorSetAllocateInfo descriptorSetAllocInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = vk.descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptorSetLayout,
    };

    VkDescriptorSet descriptorSet;
    VK_CHECK(vkAllocateDescriptorSets(vk.dev, &descriptorSetAllocInfo, &descriptorSet));

    std::array<VkWriteDescriptorSet, std::size(desc.entries)> writes;
    CHECK_LE(desc.entriesCount, std::size(desc.entries));

    std::array<VkDescriptorBufferInfo, std::size(desc.entries)> bufferInfos;
    std::array<VkDescriptorImageInfo, std::size(desc.entries)> imageInfos;

    CHECK_EQ(desc.entriesCount, layoutData.desc.bindingCount);
    for (uint32_t i = 0; i < desc.entriesCount; ++i)
    {
        const RhiBindGroupEntry &bindEntry = desc.entries[i];
        const RhiBindingDesc &layoutEntry = layoutData.desc.bindings[i];
        CHECK_EQ(bindEntry.binding, layoutEntry.binding);

        writes[i] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = descriptorSet,
            .dstBinding = bindEntry.binding,
            .dstArrayElement = bindEntry.arrayIndex,
            .descriptorCount = layoutEntry.arraySize,
            .descriptorType = ConvertVulkanBindingType(layoutEntry.type, layoutEntry.flags),
        };

        switch (layoutEntry.type)
        {
        case RhiBindingType::UniformBuffer: {
            const VulkanBufferData &bufferData = HandleGetData(rhiHandles.buffers, bindEntry.buffer.buffer);
            const VkDescriptorBufferInfo &bufferInfo = bufferInfos[i] = VkDescriptorBufferInfo{
                .buffer = bufferData.buffer,
                .offset = bindEntry.buffer.offset,
                .range = bindEntry.buffer.range,
            };

            writes[i].pBufferInfo = &bufferInfo;
            break;
        }

        case RhiBindingType::Texture: {
            const VulkanTextureData &textureData = HandleGetData(rhiHandles.textures, bindEntry.texture.texture);
            const VkDescriptorImageInfo &imageInfo = imageInfos[i] = VkDescriptorImageInfo{
                .imageView = textureData.imageView,
                .imageLayout = textureData.layout,
            };

            writes[i].pImageInfo = &imageInfo;
            break;
        }

        case RhiBindingType::Sampler: {
            const VulkanSamplerData &samplerData = HandleGetData(rhiHandles.samplers, bindEntry.sampler.sampler);
            const VkDescriptorImageInfo &imageInfo = imageInfos[i] = VkDescriptorImageInfo{
                .sampler = samplerData.sampler,
            };

            writes[i].pImageInfo = &imageInfo;
            break;
        }

        default: {
            CHECK(false);
        }
        }
    }

    vkUpdateDescriptorSets(vk.dev, desc.entriesCount, writes.data(), 0, nullptr);

    return HandleAcquire(rhiHandles.bindGroups, descriptorSet);
}

void RhiDestroyBindGroup(RhiBindGroup bindGroup)
{
    VkDescriptorSet descriptorSet = HandleRelease(rhiHandles.bindGroups, bindGroup);
    vkFreeDescriptorSets(vk.dev, vk.descriptorPool, 1, &descriptorSet);
}

void RhiCmdBindGraphicsBindGroup(RhiCmdList cmd, uint32_t setIndex, RhiBindGroup bindGroup,
                                 std::span<const uint32_t> dynamicOffsets)
{ // TODO: validate dynamic offsets size !!!
    const VulkanCmdListData &cmdData = HandleGetData(rhiHandles.cmdLists, cmd);
    const VulkanPipelineData &pipelineData = HandleGetData(rhiHandles.graphicsPipelines, cmdData.boundGraphicsPipeline);
    const VkDescriptorSet &descriptorSet = HandleGetData(rhiHandles.bindGroups, bindGroup);

    vkCmdBindDescriptorSets(cmdData.cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.layout, setIndex, 1,
                            &descriptorSet, dynamicOffsets.size(), dynamicOffsets.data());
}

} // namespace nyla