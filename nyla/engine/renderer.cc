#include "nyla/engine/renderer.h"
#include "nyla/commons/byteview.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/math/mat.h"
#include "nyla/commons/math/vec.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/engine0_internal.h"
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

void Renderer::Init()
{
    const RhiShader vs = GetShader("renderer.vs", RhiShaderStage::Vertex);
    const RhiShader ps = GetShader("renderer.ps", RhiShaderStage::Pixel);

    auto *renderer = new Renderer{};

    const RhiGraphicsPipelineDesc pipelineDesc{
        .debugName = "Renderer",
        .vs = vs,
        .ps = ps,
        .vertexBindingsCount = 1,
        .vertexBindings = AssetManager::GetMeshVertexBindings(),
        .vertexAttributeCount = 3,
        .vertexAttributes = AssetManager::GetMeshVertexAttributes(),
        .colorTargetFormatsCount = 1,
        .colorTargetFormats = AssetManager::GetMeshPipelineColorTargetFormats(),
        .cullMode = RhiCullMode::None,
        .frontFace = RhiFrontFace::CCW,
    };

    m_Pipeline = g_Rhi.CreateGraphicsPipeline(pipelineDesc);

    auto &assetManager = g_Engine.GetAssetManager();

    { // RECT MESH
        auto &alloc = g_Engine.GetPermanentAlloc();

        std::span<AssetManager::MeshVSInput> vertices = alloc.PushArr<AssetManager::MeshVSInput>(4);

        vertices[0] = AssetManager::MeshVSInput{
            .pos = {-.5f, .5f, .0f},
            .normal = {0.f, 0.f, -1.f},
            .uv = {1.f, 0.f},
        };
        vertices[1] = AssetManager::MeshVSInput{
            .pos = {.5f, -.5f, .0f},
            .normal = {0.f, 0.f, -1.f},
            .uv = {0.f, 1.f},
        };
        vertices[2] = AssetManager::MeshVSInput{
            .pos = {.5f, .5f, .0f},
            .normal = {0.f, 0.f, -1.f},
            .uv = {0.f, 0.f},
        };
        vertices[3] = AssetManager::MeshVSInput{
            .pos = {-.5f, -.5f, .0f},
            .normal = {0.f, 0.f, -1.f},
            .uv = {1.f, 1.f},
        };

        std::span<uint16_t> indices = alloc.PushArr<uint16_t>(6);

        indices[0] = 0;
        indices[1] = 1;
        indices[2] = 2;
        indices[3] = 0;
        indices[4] = 3;
        indices[5] = 1;

#if 0
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
#endif

        m_RectMesh = assetManager.DeclareStaticMesh({(char *)vertices.data(), vertices.size_bytes()}, indices);
    }
}

namespace
{

auto GetRectTransform(float2 pos, float2 size)
{
    auto model = float4x4::Identity();
    model = model.Mult(float4x4::Translate(float4{pos[0], pos[1], 0, 1}));
    model = model.Mult(float4x4::Scale(float4{size[0], size[1], 1, 1}));
    return model;
}

} // namespace

void Renderer::Rect(float2 pos, float2 dimensions, AssetManager::Texture texture)
{
    auto &assetManager = g_Engine.GetAssetManager();

    RhiSampledTextureView srv;
    if (!assetManager.GetRhiSampledTextureView(texture, srv))
        return;

    auto &entity = m_DrawQueue.emplace_back(Entity{});
    entity.model = GetRectTransform(pos, dimensions);

    entity.srvTextureIndex = srv.index;
    entity.samplerIndex = uint32_t(AssetManager::SamplerType::NearestClamp);
}

void Renderer::Rect(float2 pos, float2 dimensions, float4 color)
{
    auto &entity = m_DrawQueue.emplace_back(Entity{});
    entity.model = GetRectTransform(pos, dimensions);

    entity.color = color;
}

void Renderer::CmdFlush(RhiCmdList cmd, uint32_t width, uint32_t height, float metersOnScreen)
{
    g_Rhi.CmdBindGraphicsPipeline(cmd, m_Pipeline);

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

    g_Rhi.SetPassConstant(cmd, ByteViewPtr(&scene));

    auto &assetManager = g_Engine.GetAssetManager();
    assetManager.CmdBindMesh(cmd, m_RectMesh);

    const uint32_t frameIndex = g_Rhi.GetFrameIndex();

    for (const Entity &entityUbo : m_DrawQueue)
    {
        g_Rhi.SetDrawConstant(cmd, ByteViewPtr(&entityUbo));
        g_Rhi.CmdDrawIndexed(cmd, 6, 0, 1, 0, 0);
    }
    m_DrawQueue.clear();
}

} // namespace nyla