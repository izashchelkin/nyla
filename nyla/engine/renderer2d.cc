#include "nyla/engine/renderer2d.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/math/mat.h"
#include "nyla/commons/math/vec.h"
#include "nyla/commons/memory/align.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/engine0_internal.h"
#include "nyla/engine/staging_buffer.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_descriptor.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/rhi/rhi_texture.h"
#include <cstdint>

namespace nyla
{

using namespace engine0_internal;

namespace
{

struct VSInput
{
    float4 pos;
    float4 color;
    float2 uv;
};

struct EntityUbo
{
    float4x4 model;
    float4 color;
    uint32_t textureIndex;
    uint32_t samplerIndex;
};

struct Scene
{
    float4x4 vp;
    float4x4 invVp;
};

} // namespace

struct Renderer2D
{
    RhiGraphicsPipeline pipeline;
    RhiDescriptorSetLayout descriptorSetLayout;
    RhiBuffer vertexBuffer;
    std::array<RhiDescriptorSet, kRhiMaxNumFramesInFlight> descriptorSets;
    std::array<RhiBuffer, kRhiMaxNumFramesInFlight> dynamicUniformBuffer;
    uint32_t dymamicUniformBufferWritten;

    InlineVec<uint32_t, 256> pendingDraws;
};

auto CreateRenderer2D() -> Renderer2D *
{
    const RhiShader vs = GetShader("renderer2d.vs", RhiShaderStage::Vertex);
    const RhiShader ps = GetShader("renderer2d.ps", RhiShaderStage::Pixel);

    auto *renderer = new Renderer2D{};

    const std::array<RhiDescriptorLayoutDesc, 1> descriptorLayouts{
        RhiDescriptorLayoutDesc{
            .binding = 0,
            .type = RhiBindingType::UniformBuffer,
            .flags = RhiDescriptorFlags::Dynamic,
            .arraySize = 1,
            .stageFlags = RhiShaderStage::Vertex | RhiShaderStage::Pixel,
        },
    };
    renderer->descriptorSetLayout = g_Rhi->CreateDescriptorSetLayout(RhiDescriptorSetLayoutDesc{
        .descriptors = descriptorLayouts,
    });

    const RhiGraphicsPipelineDesc pipelineDesc{
        .debugName = "Renderer2D",
        .vs = vs,
        .ps = ps,
        .bindGroupLayoutsCount = 2,
        .bindGroupLayouts =
            {
                renderer->descriptorSetLayout,
                g_AssetManager->GetDescriptorSetLayout(),
            },
        .vertexBindingsCount = 1,
        .vertexBindings =
            {
                RhiVertexBindingDesc{
                    .binding = 0,
                    .stride = sizeof(VSInput),
                    .inputRate = RhiInputRate::PerVertex,
                },
            },
        .vertexAttributeCount = 3,
        .vertexAttributes =
            {
                RhiVertexAttributeDesc{
                    .binding = 0,
                    .location = 0,
                    .format = RhiVertexFormat::R32G32B32A32Float,
                    .offset = offsetof(VSInput, pos),
                },
                RhiVertexAttributeDesc{
                    .binding = 0,
                    .location = 1,
                    .format = RhiVertexFormat::R32G32B32A32Float,
                    .offset = offsetof(VSInput, color),
                },
                RhiVertexAttributeDesc{
                    .binding = 0,
                    .location = 2,
                    .format = RhiVertexFormat::R32G32Float,
                    .offset = offsetof(VSInput, uv),
                },
            },
        .colorTargetFormatsCount = 1,
        .colorTargetFormats =
            {
                g_Rhi->GetTextureInfo(g_Rhi->GetBackbufferTexture()).format,
            },
        .pushConstantSize = sizeof(Scene),
        .cullMode = RhiCullMode::None,
        .frontFace = RhiFrontFace::CCW,
    };

    renderer->pipeline = g_Rhi->CreateGraphicsPipeline(pipelineDesc);

    constexpr uint32_t kVertexBufferSize = 1 << 20;
    renderer->vertexBuffer = g_Rhi->CreateBuffer(RhiBufferDesc{
        .size = kVertexBufferSize,
        .bufferUsage = RhiBufferUsage::Vertex | RhiBufferUsage::CopyDst,
        .memoryUsage = RhiMemoryUsage::GpuOnly,
    });

    for (uint32_t i = 0; i < g_Rhi->GetNumFramesInFlight(); ++i)
    {
        constexpr uint32_t kDynamicUniformBufferSize = 1 << 20;
        renderer->dynamicUniformBuffer[i] = g_Rhi->CreateBuffer(RhiBufferDesc{
            .size = kDynamicUniformBufferSize,
            .bufferUsage = RhiBufferUsage::Uniform,
            .memoryUsage = RhiMemoryUsage::CpuToGpu,
        });

        renderer->descriptorSets[i] = g_Rhi->CreateDescriptorSet(renderer->descriptorSetLayout);

        const std::array<RhiDescriptorWriteDesc, 1> descriptorWrites{
            RhiDescriptorWriteDesc{
                .set = renderer->descriptorSets[i],
                .binding = 0,
                .arrayIndex = 0,
                .type = RhiBindingType::UniformBuffer,
                .resourceBinding = {.buffer = RhiBufferBinding{.buffer = renderer->dynamicUniformBuffer[i],
                                                               .size = kDynamicUniformBufferSize,
                                                               .offset = 0,
                                                               .range = sizeof(EntityUbo)}},
            },
        };
        g_Rhi->WriteDescriptors(descriptorWrites);
    }

    return renderer;
}

void Renderer2DFrameBegin(RhiCmdList cmd, Renderer2D *renderer, GpuStagingBuffer *stagingBuffer)
{
    static bool uploadedVertices = false;
    if (!uploadedVertices)
    {
        g_Rhi->CmdTransitionBuffer(cmd, renderer->vertexBuffer, RhiBufferState::CopyDst);

        char *uploadMemory =
            StagingBufferCopyIntoBuffer(cmd, stagingBuffer, renderer->vertexBuffer, 0, 6 * sizeof(VSInput));
        new (uploadMemory) std::array<VSInput, 6>{
            VSInput{
                .pos = {-.5f, .5f, .0f, 1.f},
                .color = {},
                .uv = {1.f, 0.f},
            },
            VSInput{
                .pos = {.5f, -.5f, .0f, 1.f},
                .color = {},
                .uv = {0.f, 1.f},
            },
            VSInput{
                .pos = {.5f, .5f, .0f, 1.f},
                .uv = {0.f, 0.f},
            },

            VSInput{
                .pos = {-.5f, .5f, .0f, 1.f},
                .color = {},
                .uv = {1.f, 0.f},
            },
            VSInput{
                .pos = {-.5f, -.5f, .0f, 1.f},
                .color = {},
                .uv = {1.f, 1.f},
            },
            VSInput{
                .pos = {.5f, -.5f, .0f, 1.f},
                .color = {},
                .uv = {0.f, 1.f},
            },
        };

        g_Rhi->CmdTransitionBuffer(cmd, renderer->vertexBuffer, RhiBufferState::ShaderRead);

        uploadedVertices = true;
    }
}

void Renderer2DRect(RhiCmdList cmd, Renderer2D *renderer, float x, float y, float width, float height, float4 color,
                    uint32_t textureIndex)
{
    const uint32_t frameIndex = g_Rhi->GetFrameIndex();

    AlignUp(renderer->dymamicUniformBufferWritten, g_Rhi->GetMinUniformBufferOffsetAlignment());
    auto *p = g_Rhi->MapBuffer(renderer->dynamicUniformBuffer[frameIndex]) + renderer->dymamicUniformBufferWritten;
    new (p) EntityUbo{
        .model = float4x4::Translate(float4{x, y, 0, 1}).Mult(float4x4::Scale(float4{width, height, 1, 1})),
        .color = color,
        .textureIndex = textureIndex,
        .samplerIndex = uint32_t(AssetManager::SamplerType::NearestClamp),
    };

    uint32_t offset = renderer->dymamicUniformBufferWritten;
    renderer->dymamicUniformBufferWritten += sizeof(EntityUbo);

    renderer->pendingDraws.emplace_back(offset);
}

void Renderer2DDraw(RhiCmdList cmd, Renderer2D *renderer, uint32_t width, uint32_t height, float metersOnScreen)
{
    g_Rhi->CmdBindGraphicsPipeline(cmd, renderer->pipeline);

    g_AssetManager->BindDescriptorSet(cmd);

    float worldW;
    float worldH;

    const float base = metersOnScreen;
    const float aspect = ((float)width) / ((float)height);

    worldH = base;
    worldW = base * aspect;

    float4x4 view = float4x4::Identity();
    float4x4 proj = float4x4::Ortho(-worldW * .5f, worldW * .5f, worldH * .5f, -worldH * .5f, 0.f, 1.f);

    float4x4 vp = proj.Mult(view);
    float4x4 invVp = vp.Inversed();
    Scene scene = {
        .vp = vp,
        .invVp = invVp,
    };

    g_Rhi->CmdPushGraphicsConstants(cmd, 0, RhiShaderStage::Vertex | RhiShaderStage::Pixel, ByteViewPtr(&scene));

    std::array<RhiBuffer, 1> buffers{renderer->vertexBuffer};
    std::array<uint32_t, 1> offsets{0};

    g_Rhi->CmdBindVertexBuffers(cmd, 0, buffers, offsets);

    renderer->dymamicUniformBufferWritten = 0;

    const uint32_t frameIndex = g_Rhi->GetFrameIndex();

    for (uint32_t offset : renderer->pendingDraws)
    {
        g_Rhi->CmdBindGraphicsBindGroup(cmd, 0, renderer->descriptorSets[frameIndex], {&offset, 1});
        g_Rhi->CmdDraw(cmd, 6, 1, 0, 0);
    }
    renderer->pendingDraws.clear();
}

} // namespace nyla