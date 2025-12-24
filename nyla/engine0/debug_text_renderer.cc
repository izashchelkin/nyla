#include "nyla/engine0/debug_text_renderer.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/memory/align.h"
#include "nyla/engine0/engine0_internal.h"

#include <cstdint>

#include "nyla/commons/math/vec.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_descriptor.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/rhi_shader.h"

namespace nyla
{

using namespace engine0_internal;

namespace
{

struct DrawData
{
    std::array<uint4, 17> words;
    int32_t originX;
    int32_t originY;
    uint32_t wordCount;
    uint32_t pad;
    std::array<float, 4> fg;
    std::array<float, 4> bg;
};
InlineVec<DrawData, 4> pendingDebugTextDraws;

} // namespace

struct DebugTextRenderer
{
    RhiGraphicsPipeline pipeline;
    RhiDescriptorSetLayout bindGroupLayout;
    std::array<RhiDescriptorSet, kRhiMaxNumFramesInFlight> bindGroup;
    std::array<RhiBuffer, kRhiMaxNumFramesInFlight> dynamicUniformBuffer;
    uint32_t dymamicUniformBufferWritten;
};

auto CreateDebugTextRenderer() -> DebugTextRenderer *
{
    const RhiShader vs = GetShader("fullscreen.vs", RhiShaderStage::Vertex);
    const RhiShader ps = GetShader("debug_text_renderer.ps", RhiShaderStage::Pixel);

    auto *renderer = new DebugTextRenderer{};

    const RhiDescriptorLayoutDesc descriptorLayout{
        .binding = 0,
        .type = RhiBindingType::UniformBuffer,
        .flags = RhiDescriptorFlags::Dynamic,
        .arraySize = 1,
        .stageFlags = RhiShaderStage::Pixel,
    };
    renderer->bindGroupLayout = RhiCreateDescriptorSetLayout(RhiDescriptorSetLayoutDesc{
        .descriptors = std::span{&descriptorLayout, 1},
    });

    const RhiGraphicsPipelineDesc pipelineDesc{
        .debugName = "Renderer2D",
        .vs = vs,
        .ps = ps,
        .bindGroupLayoutsCount = 1,
        .bindGroupLayouts =
            {
                renderer->bindGroupLayout,
            },
        .colorTargetFormatsCount = 1,
        .colorTargetFormats =
            {
                RhiGetTextureInfo(RhiGetBackbufferTexture()).format,
            },
    };
    renderer->pipeline = RhiCreateGraphicsPipeline(pipelineDesc);

    for (uint32_t i = 0; i < RhiGetNumFramesInFlight(); ++i)
    {
        constexpr uint32_t kDynamicUniformBufferSize = 1 << 10;
        renderer->dynamicUniformBuffer[i] = RhiCreateBuffer(RhiBufferDesc{
            .size = kDynamicUniformBufferSize,
            .bufferUsage = RhiBufferUsage::Uniform,
            .memoryUsage = RhiMemoryUsage::CpuToGpu,
        });

        renderer->bindGroup[i] = RhiCreateDescriptorSet(renderer->bindGroupLayout);

        const RhiDescriptorWriteDesc descriptorWrite{
            .set = renderer->bindGroup[i],
            .binding = 0,
            .arrayIndex = 0,
            .type = RhiBindingType::UniformBuffer,
            .buffer =
                RhiBufferBinding{
                    .buffer = renderer->dynamicUniformBuffer[i],
                    .size = kDynamicUniformBufferSize,
                    .offset = 0,
                    .range = sizeof(DrawData),
                },
        };
        RhiWriteDescriptors(std::span{&descriptorWrite, 1});
    }

    return renderer;
}

void DebugText(int32_t x, int32_t y, std::string_view text)
{
    DrawData drawData{
        .originX = x,
        .originY = y,
        .wordCount = std::min<uint32_t>((text.size() + 3) / 4, 68),
        .fg = {1.f, 1.f, 1.f, 1.f},
    };

    const size_t numBytes = std::min(text.size(), size_t(drawData.wordCount) * 4);

    for (size_t i = 0; i < drawData.wordCount; ++i)
    {
        uint32_t word = 0;
        for (uint8_t j = 0; j < 4; ++j)
        {
            const uint32_t idx = i * 4 + j;
            if (idx < numBytes)
                word |= (uint32_t(uint8_t(text[idx])) << (8 * j));
        }

        drawData.words[i / 4][i % 4] = word;
    }

    pendingDebugTextDraws.emplace_back(drawData);
}

void DebugTextRendererDraw(RhiCmdList cmd, DebugTextRenderer *renderer)
{
    if (pendingDebugTextDraws.empty())
        return;

    const uint32_t frameIndex = RhiGetFrameIndex();

    RhiCmdBindGraphicsPipeline(cmd, renderer->pipeline);

    for (const DrawData &drawData : pendingDebugTextDraws)
    {
        AlignUp(renderer->dymamicUniformBufferWritten, RhiGetMinUniformBufferOffsetAlignment());
        new (RhiMapBuffer(renderer->dynamicUniformBuffer[frameIndex]) + renderer->dymamicUniformBufferWritten)
            DrawData{drawData};

        const uint32_t offset = renderer->dymamicUniformBufferWritten;
        renderer->dymamicUniformBufferWritten += sizeof(DrawData);

        RhiCmdBindGraphicsBindGroup(cmd, 0, renderer->bindGroup[frameIndex], {&offset, 1});
        RhiCmdDraw(cmd, 3, 1, 0, 0);
    }
    pendingDebugTextDraws.clear();

    renderer->dymamicUniformBufferWritten = 0;
}

} // namespace nyla