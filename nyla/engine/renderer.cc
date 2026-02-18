#include "nyla/engine/renderer.h"
#include "nyla/apps/breakout/breakout.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/byteview.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/handle.h"
#include "nyla/commons/math/mat.h"
#include "nyla/commons/math/vec.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/engine0_internal.h"
#include "nyla/rhi/rhi.h"
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
        .vertexBindings = AssetManager::GetMeshVertexBindings(),
        .vertexAttributes = AssetManager::GetMeshVertexAttributes(),
        .colorTargetFormats = AssetManager::GetMeshPipelineColorTargetFormats(),
        .depthFormat = RhiTextureFormat::D32_Float_S8_UINT,
        .cullMode = RhiCullMode::Back,
        .frontFace = RhiFrontFace::CCW,
    };

    m_Pipeline = g_Rhi.CreateGraphicsPipeline(pipelineDesc);
}

void Renderer::Mesh(float3 pos, float3 scale, AssetManager::Mesh mesh, AssetManager::Texture texture)
{
    auto &assetManager = g_Engine.GetAssetManager();

    RhiSampledTextureView srv;
    if (HandleIsSet(texture))
    {
        if (!assetManager.GetRhiSampledTextureView(texture, srv))
            return;
    }
    else
    {
        if (!assetManager.GetRhiSampledTextureView(mesh, srv))
            return;
    }

    DrawCall &drawCall = m_DrawQueue.emplace_back(DrawCall{});
    drawCall.mesh = mesh;

    Entity &entity = drawCall.entity;

    entity.model = float4x4::Identity();
    entity.model = entity.model.Mult(float4x4::Translate(float4{pos[0], pos[1], pos[2], 1}));
    entity.model = entity.model.Mult(float4x4::Scale(float4{scale[0], scale[1], scale[2], 1}));

    entity.srvTextureIndex = srv.index;

    entity.samplerIndex = uint32_t(AssetManager::SamplerType::NearestClamp);
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

    const uint32_t frameIndex = g_Rhi.GetFrameIndex();

    auto &assetManager = g_Engine.GetAssetManager();
    for (const auto &drawCall : m_DrawQueue)
    {
        const auto &entity = drawCall.entity;
        g_Rhi.SetDrawConstant(cmd, ByteViewPtr(&entity));

        assetManager.CmdBindMesh(cmd, drawCall.mesh);
        assetManager.CmdDrawMesh(cmd, drawCall.mesh);
    }
    m_DrawQueue.clear();
}

} // namespace nyla