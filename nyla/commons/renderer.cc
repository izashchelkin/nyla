#include "nyla/commons/renderer.h"

#include <cstdint>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/inline_vec_def.h"
#include "nyla/commons/mat.h"
#include "nyla/commons/math.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/sampler_manager.h"
#include "nyla/commons/shader.h"
#include "nyla/commons/span.h"
#include "nyla/commons/texture_manager.h"
#include "nyla/commons/vec.h"

namespace nyla
{

namespace
{

struct scene // Per Frame
{
    float4x4 vp;
    float4x4 invVp;
};

struct entity
{
    float4x4 model;
    uint32_t srvTextureIndex;
    uint32_t samplerIndex;
};

struct draw_call
{
    entity Entity;
    mesh Mesh;
};

struct renderer_state
{
    float4x4 View;
    float4x4 Proj;
    rhi_graphics_pipeline Pipeline;
    inline_vec<draw_call, 256> DrawQueue;
};
renderer_state *renderer;

} // namespace

namespace Renderer
{

void API Bootstrap(region_alloc &alloc)
{
    renderer = &RegionAlloc::Alloc<renderer_state>(RegionAlloc::g_BootstrapAlloc);

    rhi_shader vs = GetShader(alloc, "renderer.vs"_s, rhi_shader_stage::Vertex);
    rhi_shader ps = GetShader(alloc, "renderer.ps"_s, rhi_shader_stage::Pixel);

    array<rhi_vertex_attribute_desc, 3> vertexAttributes{
        rhi_vertex_attribute_desc{
            .binding = 0,
            .semantic = "POSITION0"_s,
            .format = rhi_vertex_format::R32G32B32Float,
            .offset = 0,
        },
        rhi_vertex_attribute_desc{
            .binding = 0,
            .semantic = "NORMAL0"_s,
            .format = rhi_vertex_format::R32G32B32Float,
            .offset = 12,
        },
        rhi_vertex_attribute_desc{
            .binding = 0,
            .semantic = "TEXCOORD0"_s,
            .format = rhi_vertex_format::R32G32Float,
            .offset = 24,
        },
    };

    rhi_vertex_binding_desc vertexBinding{
        .binding = 0,
        .stride = 32,
        .inputRate = rhi_input_rate::PerVertex,
    };

    rhi_texture_format colorFormat = rhi_texture_format::B8G8R8A8_sRGB;

    const rhi_graphics_pipeline_desc pipelineDesc{
        .debugName = "Renderer"_s,
        .vs = vs,
        .ps = ps,
        .vertexBindings = {&vertexBinding, 1},
        .vertexAttributes = vertexAttributes,
        .colorTargetFormats = {&colorFormat, 1},
        .depthFormat = rhi_texture_format::D32_Float_S8_UINT,
        .depthWriteEnabled = true,
        .depthTestEnabled = true,
        .cullMode = rhi_cull_mode::Back,
        .frontFace = rhi_front_face::CCW,
    };

    renderer->Pipeline = Rhi::CreateGraphicsPipeline(alloc, pipelineDesc);
}

void API SetView(float4x4 m)
{
    renderer->View = m;
}

void API SetLookAtView(float3 eye, float3 center, float3 up)
{
    renderer->View = Mat::LookAt(eye, center, up);
}

void API SetProjection(float4x4 m)
{
    renderer->Proj = m;
}

void API SetOrthoProjection(uint32_t width, uint32_t height, float metersOnScreen)
{
    float worldW;
    float worldH;

    const float base = metersOnScreen;
    const float aspect = ((float)width) / ((float)height);

    worldH = base;
    worldW = base * aspect;

    renderer->Proj = Mat::Ortho(-worldW * .5f, worldW * .5f, worldH * .5f, -worldH * .5f, 0.f, 1.f);
}

void API SetPerspectiveProjection(uint32_t width, uint32_t height, float fovDegrees, float nearPlane, float farPlane)
{
    const float aspect = (float)width / (float)height;
    const float fovRadians = fovDegrees * (math::pi / 180.0f);

    renderer->Proj = Mat::Perspective(fovRadians, aspect, nearPlane, farPlane);
}

void API Mesh(float3 pos, float3 scale, mesh Mesh, texture Texture)
{
    if (!Texture)
    {
        Texture = MeshManager::GetTexture(Mesh);
        if (!Texture)
            return;
    }

    rhi_srv srv = TextureManager::GetSRV(Texture);
    if (!srv)
        return;

    draw_call &drawCall = InlineVec::Append(renderer->DrawQueue, draw_call{.Mesh = Mesh});
    entity &entity = drawCall.Entity;

    Mat::Identity(entity.model);
    entity.model = entity.model * Mat::Translate(float4{pos[0], pos[1], pos[2], 1.f});
    entity.model = entity.model * Mat::Scale(float4{scale[0], scale[1], scale[2], 1.f});

    entity.srvTextureIndex = srv.index;

    entity.samplerIndex = uint32_t(sampler_type::NearestClamp);
}

void API CmdFlush(rhi_cmdlist cmd)
{
    Rhi::CmdBindGraphicsPipeline(cmd, renderer->Pipeline);

    float4x4 vp = renderer->Proj * renderer->View;
    float4x4 invVp = Mat::Inverse(vp);
    scene scene = {
        .vp = vp,
        .invVp = invVp,
    };

    Rhi::SetPassConstant(cmd, Span::ByteViewPtr(&scene));

    const uint32_t frameIndex = Rhi::GetFrameIndex();

    for (const auto &drawCall : renderer->DrawQueue)
    {
        const auto &entity = drawCall.Entity;
        Rhi::SetDrawConstant(cmd, Span::ByteViewPtr(&entity));

        MeshManager::CmdBindMesh(cmd, drawCall.Mesh);
        MeshManager::CmdDrawMesh(cmd, drawCall.Mesh);
    }
    InlineVec::Clear(renderer->DrawQueue);
}

} // namespace Renderer

} // namespace nyla