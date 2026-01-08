#include "nyla/engine/renderer2d.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/math/mat.h"
#include "nyla/commons/math/vec.h"
#include "nyla/commons/memory/charview.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/engine0_internal.h"
#include "nyla/engine/staging_buffer.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
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

struct EntityUbo // Per Draw
{
    float4x4 model;
    float4 color;
    uint32_t textureIndex;
    uint32_t samplerIndex;
};

struct Scene // Per Frame
{
    float4x4 vp;
    float4x4 invVp;
};

} // namespace

struct Renderer2D
{
    RhiGraphicsPipeline pipeline;
    RhiBuffer vertexBuffer;

    InlineVec<EntityUbo, 256> pendingDraws;
};

auto CreateRenderer2D() -> Renderer2D *
{
    const RhiShader vs = GetShader("renderer2d.vs", RhiShaderStage::Vertex);
    const RhiShader ps = GetShader("renderer2d.ps", RhiShaderStage::Pixel);

    auto *renderer = new Renderer2D{};

    const RhiGraphicsPipelineDesc pipelineDesc{
        .debugName = "Renderer2D",
        .vs = vs,
        .ps = ps,
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
                    .semantic = "POSITION0",
                    .format = RhiVertexFormat::R32G32B32A32Float,
                    .offset = offsetof(VSInput, pos),
                },
                RhiVertexAttributeDesc{
                    .binding = 0,
                    .semantic = "COLOR0",
                    .format = RhiVertexFormat::R32G32B32A32Float,
                    .offset = offsetof(VSInput, color),
                },
                RhiVertexAttributeDesc{
                    .binding = 0,
                    .semantic = "TEXCOORD0",
                    .format = RhiVertexFormat::R32G32Float,
                    .offset = offsetof(VSInput, uv),
                },
            },
        .colorTargetFormatsCount = 1,
        .colorTargetFormats = {RhiTextureFormat::B8G8R8A8_sRGB},
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

    return renderer;
}

void Renderer2DFrameBegin(RhiCmdList cmd, Renderer2D *renderer, GpuStagingBuffer *stagingBuffer)
{
    renderer->pendingDraws.clear();

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
    renderer->pendingDraws.emplace_back(EntityUbo{
        .model = float4x4::Translate(float4{x, y, 0, 1}).Mult(float4x4::Scale(float4{width, height, 1, 1})),
        .color = color,
        .textureIndex = textureIndex,
        .samplerIndex = uint32_t(AssetManager::SamplerType::NearestClamp),
    });
}

void Renderer2DDraw(RhiCmdList cmd, Renderer2D *renderer, uint32_t width, uint32_t height, float metersOnScreen)
{
    g_Rhi->CmdBindGraphicsPipeline(cmd, renderer->pipeline);

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

    g_Rhi->SetPassConstant(cmd, ByteViewPtr(&scene));

    std::array<RhiBuffer, 1> buffers{renderer->vertexBuffer};
    std::array<uint32_t, 1> offsets{0};

    g_Rhi->CmdBindVertexBuffers(cmd, 0, buffers, offsets);

    const uint32_t frameIndex = g_Rhi->GetFrameIndex();

    for (const EntityUbo &entityUbo : renderer->pendingDraws)
    {
        g_Rhi->SetDrawConstant(cmd, ByteViewPtr(&entityUbo));
        g_Rhi->CmdDraw(cmd, 6, 1, 0, 0);
    }
    renderer->pendingDraws.clear();
}

} // namespace nyla