#pragma once

#include "nyla/commons/containers/inline_vec.h"
#include "nyla/commons/math/mat.h"
#include "nyla/commons/math/vec.h"
#include "nyla/engine/asset_manager.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pipeline.h"
#include <cstdint>
#include <numbers>

namespace nyla
{

class Renderer
{
  public:
    void Init();

    void Mesh(float3 pos, float3 scale, AssetManager::Mesh mesh, AssetManager::Texture texture);

    void CmdFlush(RhiCmdList cmd);

    void SetView(float4x4 m)
    {
        m_View = m;
    }

    void SetLookAtView(float3 eye, float3 center, float3 up)
    {
        m_View = float4x4::LookAt(eye, center, up);
    }

    void SetProjection(float4x4 m)
    {
        m_Proj = m;
    }

    void SetOrthoProjection(uint32_t width, uint32_t height, float metersOnScreen)
    {
        float worldW;
        float worldH;

        const float base = metersOnScreen;
        const float aspect = ((float)width) / ((float)height);

        worldH = base;
        worldW = base * aspect;

        m_Proj = float4x4::Ortho(-worldW * .5f, worldW * .5f, worldH * .5f, -worldH * .5f, 0.f, 1.f);
    }

    void SetPerspectiveProjection(uint32_t width, uint32_t height, float fovDegrees, float nearPlane, float farPlane)
    {
        const float aspect = (float)width / (float)height;
        const float fovRadians = fovDegrees * (std::numbers::pi_v<float> / 180.0f);

        m_Proj = float4x4::Perspective(fovRadians, aspect, nearPlane, farPlane);
    }

  private:
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
};

} // namespace nyla