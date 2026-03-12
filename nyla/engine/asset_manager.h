#pragma once

#include "nyla/alloc/region_alloc.h"
#include "nyla/commons/handle.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/path.h"
#include "nyla/commons/vec.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_pipeline.h"
#include "nyla/rhi/rhi_texture.h"
#include <cstdint>
#include <string>

namespace nyla
{

class AssetManager
{
    constexpr static uint32_t kSamplersDescriptorBinding = 0;
    constexpr static uint32_t kTexturesDescriptorBinding = 1;

  public:
    struct MeshVSInput
    {
        float3 pos;
        float3 normal;
        float2 uv;
    };

    static auto GetMeshVertexAttributes() -> std::span<RhiVertexAttributeDesc>;
    static auto GetMeshVertexBindings() -> std::span<RhiVertexBindingDesc>;
    static auto GetMeshPipelineColorTargetFormats() -> std::span<RhiTextureFormat>;

    struct Texture : Handle
    {
    };

    struct Mesh : Handle
    {
    };

    struct MeshPrimitive : Handle
    {
    };

    enum class SamplerType
    {
        LinearClamp = 0,
        LinearRepeat = 1,
        NearestClamp = 2,
        NearestRepeat = 3,
    };

    static void Init();
    static void Upload(RhiCmdList cmd);

    static void Flush();

    static auto DeclareTexture(std::string_view path) -> Texture;
    static auto GetRhiSampledTextureView(Texture, RhiSampledTextureView &) -> bool;
    static auto GetRhiSampledTextureView(Mesh, RhiSampledTextureView &) -> bool;

    static auto DeclareMesh(std::string_view path) -> Mesh;
    static auto DeclareStaticMesh(std::span<const char> vertexData, std::span<const uint16_t> indices) -> Mesh;

    static void CmdBindMesh(RhiCmdList cmd, Mesh mesh);
    static void CmdDrawMesh(RhiCmdList cmd, AssetManager::Mesh mesh);
};

} // namespace nyla