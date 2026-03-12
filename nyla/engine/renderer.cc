#include <cstdint>
#include <numbers>

#include "nyla/commons/byteview.h"
#include "nyla/commons/handle.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/mat.h"
#include "nyla/commons/vec.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/engine/engine0_internal.h"
#include "nyla/engine/renderer.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

using namespace engine0_internal;

namespace
{

float4x4 m_View = float4x4::Identity();
float4x4 m_Proj = float4x4::Identity();

RhiGraphicsPipeline m_Pipeline;

struct Scene // Per Frame
{
    float4x4 vp;
    float4x4 invVp;
};

struct Entity
{
    float4x4 model;
    uint32_t srvTextureIndex;
    uint32_t samplerIndex;
};

struct DrawCall
{
    Entity entity;
    AssetManager::Mesh mesh;
};

InlineVec<DrawCall, 256> m_DrawQueue;

} // namespace

void Renderer::SetView(float4x4 m)
{
    m_View = m;
}

void Renderer::SetLookAtView(float3 eye, float3 center, float3 up)
{
    m_View = float4x4::LookAt(eye, center, up);
}

void Renderer::SetProjection(float4x4 m)
{
    m_Proj = m;
}

void Renderer::SetOrthoProjection(uint32_t width, uint32_t height, float metersOnScreen)
{
    float worldW;
    float worldH;

    const float base = metersOnScreen;
    const float aspect = ((float)width) / ((float)height);

    worldH = base;
    worldW = base * aspect;

    m_Proj = float4x4::Ortho(-worldW * .5f, worldW * .5f, worldH * .5f, -worldH * .5f, 0.f, 1.f);
}

void Renderer::SetPerspectiveProjection(uint32_t width, uint32_t height, float fovDegrees, float nearPlane,
                                        float farPlane)
{
    const float aspect = (float)width / (float)height;
    const float fovRadians = fovDegrees * (std::numbers::pi_v<float> / 180.0f);

    m_Proj = float4x4::Perspective(fovRadians, aspect, nearPlane, farPlane);
}

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
        .depthWriteEnabled = true,
        .depthTestEnabled = true,
        .cullMode = RhiCullMode::Back,
        .frontFace = RhiFrontFace::CCW,
    };

    m_Pipeline = g_Rhi.CreateGraphicsPipeline(pipelineDesc);
}

void Renderer::Mesh(float3 pos, float3 scale, AssetManager::Mesh mesh, AssetManager::Texture texture)
{
    RhiSampledTextureView srv;
    if (HandleIsSet(texture))
    {
        if (!AssetManager::GetRhiSampledTextureView(texture, srv))
            return;
    }
    else
    {
        if (!AssetManager::GetRhiSampledTextureView(mesh, srv))
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

void Renderer::CmdFlush(RhiCmdList cmd)
{
    g_Rhi.CmdBindGraphicsPipeline(cmd, m_Pipeline);

    float4x4 vp = m_Proj.Mult(m_View);
    float4x4 invVp = vp.Inversed();
    Scene scene = {
        .vp = vp,
        .invVp = invVp,
    };

    g_Rhi.SetPassConstant(cmd, ByteViewPtr(&scene));

    const uint32_t frameIndex = g_Rhi.GetFrameIndex();

    for (const auto &drawCall : m_DrawQueue)
    {
        const auto &entity = drawCall.entity;
        g_Rhi.SetDrawConstant(cmd, ByteViewPtr(&entity));

        AssetManager::CmdBindMesh(cmd, drawCall.mesh);
        AssetManager::CmdDrawMesh(cmd, drawCall.mesh);
    }
    m_DrawQueue.clear();
}

} // namespace nyla