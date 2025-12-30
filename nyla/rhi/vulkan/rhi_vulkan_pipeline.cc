#include <array>
#include <cstdint>
#include <vulkan/vulkan_core.h>

#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"

namespace nyla
{

using namespace rhi_vulkan_internal;

namespace
{

auto ConvertVulkanCullMode(RhiCullMode cullMode) -> VkCullModeFlags
{
    switch (cullMode)
    {
    case RhiCullMode::None:
        return VK_CULL_MODE_NONE;

    case RhiCullMode::Back:
        return VK_CULL_MODE_BACK_BIT;

    case RhiCullMode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    }
    CHECK(false);
    return static_cast<VkCullModeFlags>(0);
}

auto ConvertVulkanFrontFace(RhiFrontFace frontFace) -> VkFrontFace
{
    switch (frontFace)
    {
    case RhiFrontFace::CCW:
        return VK_FRONT_FACE_COUNTER_CLOCKWISE;

    case RhiFrontFace::CW:
        return VK_FRONT_FACE_CLOCKWISE;
    }
    CHECK(false);
    return static_cast<VkFrontFace>(0);
}

auto ConvertVulkanInputRate(RhiInputRate inputRate) -> VkVertexInputRate
{
    switch (inputRate)
    {
    case RhiInputRate::PerInstance:
        return VK_VERTEX_INPUT_RATE_INSTANCE;
    case RhiInputRate::PerVertex:
        return VK_VERTEX_INPUT_RATE_VERTEX;
    }
    CHECK(false);
    return static_cast<VkVertexInputRate>(0);
}

} // namespace

auto RhiCreateShader(const RhiShaderDesc &desc) -> RhiShader
{
    const VkShaderModuleCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = desc.spirv.size_bytes(),
        .pCode = desc.spirv.data(),
    };

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(vk.dev, &createInfo, nullptr, &shaderModule));

    return rhiHandles.shaders.Acquire(shaderModule);
}

void RhiDestroyShader(RhiShader shader)
{
    VkShaderModule shaderModule = rhiHandles.shaders.ReleaseData(shader);
    vkDestroyShaderModule(vk.dev, shaderModule, nullptr);
}

auto RhiCreateGraphicsPipeline(const RhiGraphicsPipelineDesc &desc) -> RhiGraphicsPipeline
{
    VulkanPipelineData pipelineData = {
        .bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    };

    std::array<VkVertexInputBindingDescription, std::size(desc.vertexBindings)> vertexBindings;
    CHECK_LE(desc.vertexBindingsCount, std::size(desc.vertexBindings));
    for (uint32_t i = 0; i < desc.vertexBindingsCount; ++i)
    {
        const auto &binding = desc.vertexBindings[i];
        vertexBindings[i] = VkVertexInputBindingDescription{
            .binding = binding.binding,
            .stride = binding.stride,
            .inputRate = ConvertVulkanInputRate(binding.inputRate),
        };
    }

    std::array<VkVertexInputAttributeDescription, std::size(desc.vertexAttributes)> vertexAttributes;
    CHECK_LE(desc.vertexAttributeCount, std::size(desc.vertexAttributes));
    for (uint32_t i = 0; i < desc.vertexAttributeCount; ++i)
    {
        const auto &attribute = desc.vertexAttributes[i];
        vertexAttributes[i] = VkVertexInputAttributeDescription{
            .location = attribute.location,
            .binding = attribute.binding,
            .format = ConvertRhiVertexFormatIntoVkFormat(attribute.format),
            .offset = attribute.offset,
        };
    }

    const VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = desc.vertexBindingsCount,
        .pVertexBindingDescriptions = vertexBindings.data(),
        .vertexAttributeDescriptionCount = desc.vertexAttributeCount,
        .pVertexAttributeDescriptions = vertexAttributes.data(),
    };

    const VkPipelineRasterizationStateCreateInfo rasterizerCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = ConvertVulkanCullMode(desc.cullMode),
        .frontFace = ConvertVulkanFrontFace(desc.frontFace),
        .lineWidth = 1.0f,
    };

    const VkPipelineInputAssemblyStateCreateInfo inputAssemblyCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    const VkPipelineViewportStateCreateInfo viewportStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    const VkPipelineMultisampleStateCreateInfo multisamplingCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };

    const VkPipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    const VkPipelineColorBlendStateCreateInfo colorBlendingCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
        .blendConstants = {},
    };

    const auto dynamicStates = std::to_array<VkDynamicState>({
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    });

    const VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = dynamicStates.size(),
        .pDynamicStates = dynamicStates.data(),
    };

    std::array<VkFormat, std::size(desc.colorTargetFormats)> colorTargetFormats;

    CHECK_LE(desc.colorTargetFormatsCount, std::size(desc.colorTargetFormats));
    for (uint32_t i = 0; i < desc.colorTargetFormatsCount; ++i)
    {
        colorTargetFormats[i] = ConvertRhiTextureFormatIntoVkFormat(desc.colorTargetFormats[i]);
    }

    const VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = desc.colorTargetFormatsCount,
        .pColorAttachmentFormats = colorTargetFormats.data(),
    };

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = rhiHandles.shaders.ResolveData(desc.vs),
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = rhiHandles.shaders.ResolveData(desc.ps),
            .pName = "main",
        },
    };

    CHECK_LE(desc.bindGroupLayoutsCount, std::size(desc.bindGroupLayouts));
    pipelineData.bindGroupLayoutCount = desc.bindGroupLayoutsCount;
    pipelineData.bindGroupLayouts = desc.bindGroupLayouts;

    CHECK_LE(desc.pushConstantSize, kRhiMaxPushConstantSize);
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = desc.pushConstantSize,
    };

    std::array<VkDescriptorSetLayout, 4> descriptorSetLayouts;
    for (uint32_t i = 0; i < desc.bindGroupLayoutsCount; ++i)
    {
        descriptorSetLayouts[i] = rhiHandles.descriptorSetLayouts.ResolveData(desc.bindGroupLayouts[i]).layout;
    }

    const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = desc.bindGroupLayoutsCount,
        .pSetLayouts = descriptorSetLayouts.data(),
        .pushConstantRangeCount = static_cast<bool>(desc.pushConstantSize),
        .pPushConstantRanges = &pushConstantRange,
    };

    vkCreatePipelineLayout(vk.dev, &pipelineLayoutCreateInfo, nullptr, &pipelineData.layout);

    const VkGraphicsPipelineCreateInfo pipelineCreateInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &pipelineRenderingCreateInfo,
        .stageCount = stages.size(),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInputStateCreateInfo,
        .pInputAssemblyState = &inputAssemblyCreateInfo,
        .pViewportState = &viewportStateCreateInfo,
        .pRasterizationState = &rasterizerCreateInfo,
        .pMultisampleState = &multisamplingCreateInfo,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &colorBlendingCreateInfo,
        .pDynamicState = &dynamicStateCreateInfo,
        .layout = pipelineData.layout,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    VK_CHECK(
        vkCreateGraphicsPipelines(vk.dev, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipelineData.pipeline));

    return rhiHandles.graphicsPipelines.Acquire(pipelineData);
}

void RhiNameGraphicsPipeline(RhiGraphicsPipeline pipeline, std::string_view name)
{
    const VulkanPipelineData &pipelineData = rhiHandles.graphicsPipelines.ResolveData(pipeline);
    VulkanNameHandle(VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipelineData.pipeline, name);
}

void RhiDestroyGraphicsPipeline(RhiGraphicsPipeline pipeline)
{
    auto pipelineData = rhiHandles.graphicsPipelines.ReleaseData(pipeline);
    vkDeviceWaitIdle(vk.dev);

    if (pipelineData.layout)
    {
        vkDestroyPipelineLayout(vk.dev, pipelineData.layout, nullptr);
    }
    if (pipelineData.pipeline)
    {
        vkDestroyPipeline(vk.dev, pipelineData.pipeline, nullptr);
    }
}

void RhiCmdBindGraphicsPipeline(RhiCmdList cmd, RhiGraphicsPipeline pipeline)
{
    VulkanCmdListData &cmdData = rhiHandles.cmdLists.ResolveData(cmd);
    const VulkanPipelineData &pipelineData = rhiHandles.graphicsPipelines.ResolveData(pipeline);

    vkCmdBindPipeline(cmdData.cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.pipeline);
    cmdData.boundGraphicsPipeline = pipeline;
}

void RhiCmdPushGraphicsConstants(RhiCmdList cmd, uint32_t offset, RhiShaderStage stage, ByteView data)
{
    const VulkanCmdListData &cmdData = rhiHandles.cmdLists.ResolveData(cmd);
    const VulkanPipelineData &pipelineData = rhiHandles.graphicsPipelines.ResolveData(cmdData.boundGraphicsPipeline);

    vkCmdPushConstants(cmdData.cmdbuf, pipelineData.layout, ConvertRhiShaderStageIntoVkShaderStageFlags(stage), offset,
                       data.size(), data.data());
}

void RhiCmdBindVertexBuffers(RhiCmdList cmd, uint32_t firstBinding, std::span<const RhiBuffer> buffers,
                             std::span<const uint32_t> offsets)
{
    CHECK_EQ(buffers.size(), offsets.size());
    CHECK_LE(buffers.size(), 4U);

    std::array<VkBuffer, 4> vkBufs;
    std::array<VkDeviceSize, 4> vkOffsets;
    for (uint32_t i = 0; i < buffers.size(); ++i)
    {
        vkBufs[i] = rhiHandles.buffers.ResolveData(buffers[i]).buffer;
        vkOffsets[i] = offsets[i];
    }

    VulkanCmdListData cmdData = rhiHandles.cmdLists.ResolveData(cmd);
    vkCmdBindVertexBuffers(cmdData.cmdbuf, firstBinding, buffers.size(), vkBufs.data(), vkOffsets.data());
}

void RhiCmdDraw(RhiCmdList cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                uint32_t firstInstance)
{
    VulkanCmdListData cmdData = rhiHandles.cmdLists.ResolveData(cmd);
    vkCmdDraw(cmdData.cmdbuf, vertexCount, instanceCount, firstVertex, firstInstance);
}

auto RhiGetVertexFormatSize(RhiVertexFormat format) -> uint32_t
{
    switch (format)
    {
    case nyla::RhiVertexFormat::None:
        break;

    case RhiVertexFormat::R32G32B32A32Float:
        return 16;
    }
    CHECK(false);
    return 0;
}

} // namespace nyla