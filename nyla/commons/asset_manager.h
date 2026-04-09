#pragma once

#include <cstdint>

#include "nyla/commons/handle.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/vec.h"

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

    static auto GetMeshVertexAttributes() -> span<RhiVertexAttributeDesc>;
    static auto GetMeshVertexBindings() -> span<RhiVertexBindingDesc>;
    static auto GetMeshPipelineColorTargetFormats() -> span<RhiTextureFormat>;

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

    static auto DeclareTexture(byteview path) -> Texture;
    static auto GetRhiSampledTextureView(Texture, RhiSampledTextureView &) -> bool;
    static auto GetRhiSampledTextureView(Mesh, RhiSampledTextureView &) -> bool;

    static auto DeclareMesh(byteview path) -> Mesh;
    static auto DeclareStaticMesh(span<const char> vertexData, span<const uint16_t> indices) -> Mesh;

    static void CmdBindMesh(RhiCmdList cmd, Mesh mesh);
    static void CmdDrawMesh(RhiCmdList cmd, AssetManager::Mesh mesh);
};

} // namespace nyla