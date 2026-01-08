#include <algorithm>
#include <array>
#include <cstdint>
#include <vulkan/vulkan_core.h>

#include "nyla/commons/assert.h"
#include "nyla/commons/containers/inline_string.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/vulkan/rhi_vulkan.h"
#include "nyla/spirview/spirview.h"

#define spv_enable_utility_code
#if defined(__linux__)
#include <spirv/unified1/spirv.hpp>
#else
#include <spirv-headers/spirv.hpp>
#endif

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
    NYLA_ASSERT(false);
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
    NYLA_ASSERT(false);
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
    NYLA_ASSERT(false);
    return static_cast<VkVertexInputRate>(0);
}

} // namespace

auto Rhi::Impl::CreateShader(const RhiShaderDesc &desc) -> RhiShader
{
    VulkanShaderData shaderData{.spv = InlineVec<uint32_t, 4096>{desc.code}};

    auto &locations = shaderData.reflect.locations;
    auto &semantics = shaderData.reflect.semantics;
    auto &inputs = shaderData.reflect.inputs;
    auto &outputs = shaderData.reflect.outputs;

    auto view = Spirview{shaderData.spv};
    auto it = view.begin();
    auto end = view.end();

    for (; it != end; ++it)
    {
        auto operandIt = it.Operands().begin();

        switch (it.Op())
        {

        case spv::OpEntryPoint: {
            switch (spv::ExecutionModel(*operandIt++))
            {
            case spv::ExecutionModel::ExecutionModelVertex:
                NYLA_ASSERT(desc.stage == RhiShaderStage::Vertex);
                break;
            case spv::ExecutionModel::ExecutionModelFragment:
                NYLA_ASSERT(desc.stage == RhiShaderStage::Pixel);
                break;
            default:
                NYLA_ASSERT(false);
                break;
            }
            break;
        }

        case spv::OpExtension: {
            InlineString<32> name;
            Spirview::ParseStringLiteral(operandIt, name);

            if (name == "SPV_GOOGLE_hlsl_functionality1" || name == "SPV_GOOGLE_user_type")
                it.MakeNop();

            break;
        }

        case spv::OpDecorate: {

            uint32_t targetId = *operandIt++;

            auto decoration = spv::Decoration(*operandIt++);
            switch (decoration)
            {
            case spv::Decoration::DecorationLocation: {
                locations.emplace_back(VulkanShaderData::IdLocation{.id = targetId, .location = *operandIt++});
                break;
            }
            }

            break;
        }

        case spv::OpDecorateString: {

            uint32_t targetId = *operandIt++;

            auto decoration = spv::Decoration(*operandIt++);
            switch (decoration)
            {

            case spv::Decoration::DecorationUserTypeGOOGLE: {
                it.MakeNop();
                break;
            }

            case spv::Decoration::DecorationUserSemantic: {
                InlineString<16> semantic;
                Spirview::ParseStringLiteral(operandIt, semantic);
                semantic.AsciiToUpper();

                semantics.emplace_back(VulkanShaderData::IdSemantic{.id = targetId, .semantic = semantic});

                it.MakeNop();
                break;
            }
            }

            break;
        }

        case spv::OpVariable: {
            uint32_t resultType = *operandIt++;
            uint32_t resultId = *operandIt++;
            auto storageClass = static_cast<spv::StorageClass>(*operandIt++);

            switch (storageClass)
            {
            case spv::StorageClass::StorageClassInput:
                inputs.emplace_back(resultId);
                break;
            case spv::StorageClass::StorageClassOutput:
                outputs.emplace_back(resultId);
                break;
            default:
                break;
            }

            break;
        }
        }
    }

    return m_Shaders.Acquire(shaderData);
}

void Rhi::Impl::DestroyShader(RhiShader shader)
{
    m_Shaders.ReleaseData(shader);
#if 0
    VkShaderModule shaderModule = m_Shaders.ReleaseData(shader);
    vkDestroyShaderModule(m_Dev, shaderModule, nullptr);
#endif
}

auto Rhi::Impl::CreateGraphicsPipeline(const RhiGraphicsPipelineDesc &desc) -> RhiGraphicsPipeline
{
    VulkanPipelineData pipelineData = {
        .bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    };

    std::array<VkVertexInputBindingDescription, std::size(desc.vertexBindings)> vertexBindings;
    NYLA_ASSERT(desc.vertexBindingsCount <= std::size(desc.vertexBindings));
    for (uint32_t i = 0; i < desc.vertexBindingsCount; ++i)
    {
        const auto &binding = desc.vertexBindings[i];
        vertexBindings[i] = VkVertexInputBindingDescription{
            .binding = binding.binding,
            .stride = binding.stride,
            .inputRate = ConvertVulkanInputRate(binding.inputRate),
        };
    }

    VulkanShaderData &vertexShaderData = m_Shaders.ResolveData(desc.vs);
    VulkanShaderData &pixelShaderData = m_Shaders.ResolveData(desc.ps);

    InlineVec<VkVertexInputAttributeDescription, std::size(desc.vertexAttributes)> vertexAttributes;
    NYLA_ASSERT(desc.vertexAttributeCount <= std::size(desc.vertexAttributes));
    NYLA_ASSERT(desc.vertexAttributeCount == vertexShaderData.reflect.inputs.size());
    NYLA_ASSERT(vertexShaderData.reflect.outputs.size() >= pixelShaderData.reflect.inputs.size());

    for (uint32_t i = 0; i < desc.vertexAttributeCount; ++i)
    {
        const auto &attribute = desc.vertexAttributes[i];

        const auto id = [&] -> uint32_t {
            auto it = vertexShaderData.reflect.semantics.begin();
            auto end = vertexShaderData.reflect.semantics.end();
            for (; it != end; ++it)
                if (attribute.semantic == it->semantic)
                    break;
            NYLA_ASSERT(it != end);
            return it->id;
        }();

        const auto location = [&] -> uint32_t {
            auto it = vertexShaderData.reflect.locations.begin();
            auto end = vertexShaderData.reflect.locations.end();
            for (; it != end; ++it)
                if (id == it->id)
                    break;
            NYLA_ASSERT(it != end);
            return it->location;
        }();

        vertexAttributes.emplace_back(VkVertexInputAttributeDescription{
            .location = location,
            .binding = attribute.binding,
            .format = ConvertVertexFormatIntoVkFormat(attribute.format),
            .offset = attribute.offset,
        });
    }

    for (uint32_t outputId : vertexShaderData.reflect.outputs)
    {
        uint32_t location;
        {
            auto it = vertexShaderData.reflect.locations.begin();
            auto end = vertexShaderData.reflect.locations.end();
            for (; it != end; ++it)
                if (outputId == it->id)
                    break;
            if (it == end)
                continue;
            location = it->location;
        }

        const auto semantic = [&] -> const InlineString<16> & {
            auto it = vertexShaderData.reflect.semantics.begin();
            auto end = vertexShaderData.reflect.semantics.end();
            for (; it != end; ++it)
                if (it->id == outputId)
                    break;
            NYLA_ASSERT(it != end);
            return it->semantic;
        }();

        uint32_t id;
        {
            auto it = pixelShaderData.reflect.semantics.begin();
            auto end = pixelShaderData.reflect.semantics.end();
            for (; it != end; ++it)
                if (it->semantic == semantic)
                    break;
            if (it == end)
                continue;
            id = it->id;
        }

        auto view = Spirview{pixelShaderData.spv};
        auto it = view.begin();
        auto end = view.end();
        for (; it != end; ++it)
        {
            if (it.Op() != spv::OpDecorate)
                continue;

            auto operandIt = it.Operands().begin();

            const uint32_t targetId = *operandIt++;
            if (targetId != id)
                continue;

            const auto decoration = spv::Decoration(*operandIt++);
            if (decoration != spv::DecorationLocation)
                continue;

            *operandIt = location;
            break;
        }
        NYLA_ASSERT(it != end);
    }

    auto createShaderModule = [dev = this->m_Dev](std::span<const uint32_t> spv) -> VkShaderModule {
        const VkShaderModuleCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = spv.size_bytes(),
            .pCode = spv.data(),
        };

        VkShaderModule shaderModule;
        VK_CHECK(vkCreateShaderModule(dev, &createInfo, nullptr, &shaderModule));

        return shaderModule;
    };

    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = createShaderModule(vertexShaderData.spv),
            .pName = "main",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = createShaderModule(pixelShaderData.spv),
            .pName = "main",
        },
    };

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

    NYLA_ASSERT(desc.colorTargetFormatsCount <= std::size(desc.colorTargetFormats));
    for (uint32_t i = 0; i < desc.colorTargetFormatsCount; ++i)
    {
        colorTargetFormats[i] = ConvertTextureFormatIntoVkFormat(desc.colorTargetFormats[i]);
    }

    const VkPipelineRenderingCreateInfo pipelineRenderingCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = desc.colorTargetFormatsCount,
        .pColorAttachmentFormats = colorTargetFormats.data(),
    };

#if 0
    NYLA_ASSERT(desc.bindGroupLayoutsCount <= std::size(desc.bindGroupLayouts));
    pipelineData.bindGroupLayoutCount = desc.bindGroupLayoutsCount;
    pipelineData.bindGroupLayouts = desc.bindGroupLayouts;
#endif

#if 0
    NYLA_ASSERT(desc.pushConstantSize <= kRhiMaxPushConstantSize);
    const VkPushConstantRange pushConstantRange{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = desc.pushConstantSize,
    };
#endif

    const std::array<VkDescriptorSetLayout, 3> descriptorSetLayouts = {
        m_ConstantsDescriptorTable.layout,
        m_TexturesDescriptorTable.layout,
        m_SamplersDescriptorTable.layout,
    };

    const VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = descriptorSetLayouts.size(),
        .pSetLayouts = descriptorSetLayouts.data(),
#if 0
        .pushConstantRangeCount = desc.pushConstantSize > 0,
        .pPushConstantRanges = &pushConstantRange,
#endif
    };

    vkCreatePipelineLayout(m_Dev, &pipelineLayoutCreateInfo, nullptr, &pipelineData.layout);

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

    VK_CHECK(vkCreateGraphicsPipelines(m_Dev, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipelineData.pipeline));

    return m_GraphicsPipelines.Acquire(pipelineData);
}

void Rhi::Impl::NameGraphicsPipeline(RhiGraphicsPipeline pipeline, std::string_view name)
{
    const VulkanPipelineData &pipelineData = m_GraphicsPipelines.ResolveData(pipeline);
    VulkanNameHandle(VK_OBJECT_TYPE_PIPELINE, (uint64_t)pipelineData.pipeline, name);
}

void Rhi::Impl::DestroyGraphicsPipeline(RhiGraphicsPipeline pipeline)
{
    auto pipelineData = m_GraphicsPipelines.ReleaseData(pipeline);
    vkDeviceWaitIdle(m_Dev);

    if (pipelineData.layout)
    {
        vkDestroyPipelineLayout(m_Dev, pipelineData.layout, nullptr);
    }
    if (pipelineData.pipeline)
    {
        vkDestroyPipeline(m_Dev, pipelineData.pipeline, nullptr);
    }
}

void Rhi::Impl::CmdBindGraphicsPipeline(RhiCmdList cmd, RhiGraphicsPipeline pipeline)
{
    VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;

    const VulkanPipelineData &pipelineData = m_GraphicsPipelines.ResolveData(pipeline);

    vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.pipeline);
    cmdData.boundGraphicsPipeline = pipeline;

    std::array<VkDescriptorSet, 2> descriptorSets{
        m_TexturesDescriptorTable.set,
        m_SamplersDescriptorTable.set,
    };

    vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.layout, 1, descriptorSets.size(),
                            descriptorSets.data(), 0, nullptr);
}

void Rhi::Impl::CmdPushGraphicsConstants(RhiCmdList cmd, uint32_t offset, RhiShaderStage stage, ByteView data)
{
    const VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    const VulkanPipelineData &pipelineData = m_GraphicsPipelines.ResolveData(cmdData.boundGraphicsPipeline);

    vkCmdPushConstants(cmdData.cmdbuf, pipelineData.layout, ConvertShaderStageIntoVkShaderStageFlags(stage), offset,
                       data.size(), data.data());
}

void Rhi::Impl::CmdBindVertexBuffers(RhiCmdList cmd, uint32_t firstBinding, std::span<const RhiBuffer> buffers,
                                     std::span<const uint32_t> offsets)
{
    NYLA_ASSERT(buffers.size() == offsets.size());
    NYLA_ASSERT(buffers.size() <= 4U);

    std::array<VkBuffer, 4> vkBufs;
    std::array<VkDeviceSize, 4> vkOffsets;
    for (uint32_t i = 0; i < buffers.size(); ++i)
    {
        vkBufs[i] = m_Buffers.ResolveData(buffers[i]).buffer;
        vkOffsets[i] = offsets[i];
    }

    const VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    vkCmdBindVertexBuffers(cmdData.cmdbuf, firstBinding, buffers.size(), vkBufs.data(), vkOffsets.data());
}

void Rhi::Impl::CmdDraw(RhiCmdList cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                        uint32_t firstInstance)
{
    VulkanCmdListData &cmdData = m_CmdLists.ResolveData(cmd);
    VkCommandBuffer cmdbuf = cmdData.cmdbuf;
    const VulkanPipelineData &pipelineData = m_GraphicsPipelines.ResolveData(cmdData.boundGraphicsPipeline);

    const std::array<uint32_t, 4> offsets{
        cmdData.frameConstantHead,
        cmdData.passConstantHead,
        cmdData.drawConstantHead,
        cmdData.largeDrawConstantHead,
    };

    vkCmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineData.layout, 0, 1,
                            &m_ConstantsDescriptorTable.set, offsets.size(), offsets.data());

    vkCmdDraw(cmdbuf, vertexCount, instanceCount, firstVertex, firstInstance);

    cmdData.drawConstantHead += CbvOffset(m_Limits.drawConstantSize);
    cmdData.largeDrawConstantHead += CbvOffset(m_Limits.largeDrawConstantSize);
}

auto Rhi::Impl::GetVertexFormatSize(RhiVertexFormat format) -> uint32_t
{
    switch (format)
    {
    case nyla::RhiVertexFormat::None:
        break;

    case RhiVertexFormat::R32G32B32A32Float:
        return 16;
    }
    NYLA_ASSERT(false);
    return 0;
}

//

auto Rhi::CreateShader(const RhiShaderDesc &desc) -> RhiShader
{
    return m_Impl->CreateShader(desc);
}

void Rhi::DestroyShader(RhiShader shader)
{
    m_Impl->DestroyShader(shader);
}

auto Rhi::GetVertexFormatSize(RhiVertexFormat format) -> uint32_t
{
    return m_Impl->GetVertexFormatSize(format);
}

auto Rhi::CreateGraphicsPipeline(const RhiGraphicsPipelineDesc &desc) -> RhiGraphicsPipeline
{
    return m_Impl->CreateGraphicsPipeline(desc);
}

void Rhi::NameGraphicsPipeline(RhiGraphicsPipeline pipeline, std::string_view name)
{
    m_Impl->NameGraphicsPipeline(pipeline, name);
}

void Rhi::DestroyGraphicsPipeline(RhiGraphicsPipeline pipeline)
{
    m_Impl->DestroyGraphicsPipeline(pipeline);
}

void Rhi::CmdBindGraphicsPipeline(RhiCmdList cmd, RhiGraphicsPipeline pipeline)
{
    m_Impl->CmdBindGraphicsPipeline(cmd, pipeline);
}

void Rhi::CmdBindVertexBuffers(RhiCmdList cmd, uint32_t firstBinding, std::span<const RhiBuffer> buffers,
                               std::span<const uint32_t> offsets)
{
    m_Impl->CmdBindVertexBuffers(cmd, firstBinding, buffers, offsets);
}

#if 0
void Rhi::CmdBindGraphicsBindGroup(RhiCmdList cmd, uint32_t setIndex, RhiDescriptorSet bindGroup,
                                   std::span<const uint32_t> dynamicOffsets)
{
    m_Impl->CmdBindGraphicsBindGroup(cmd, setIndex, bindGroup, dynamicOffsets);
}
#endif

void Rhi::CmdPushGraphicsConstants(RhiCmdList cmd, uint32_t offset, RhiShaderStage stage, ByteView data)
{
    m_Impl->CmdPushGraphicsConstants(cmd, offset, stage, data);
}

void Rhi::CmdDraw(RhiCmdList cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                  uint32_t firstInstance)
{
    m_Impl->CmdDraw(cmd, vertexCount, instanceCount, firstVertex, firstInstance);
}

} // namespace nyla