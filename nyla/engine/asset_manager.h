#pragma once

#include "nyla/commons/handle.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/math/vec.h"
#include "nyla/commons/memory/region_alloc.h"
#include "nyla/commons/path.h"
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

    static auto GetMeshVertexAttributes() -> std::span<RhiVertexAttributeDesc>
    {
        static std::array<RhiVertexAttributeDesc, 3> ret{
            RhiVertexAttributeDesc{
                .binding = 0,
                .semantic = "POSITION0",
                .format = RhiVertexFormat::R32G32B32Float,
                .offset = offsetof(AssetManager::MeshVSInput, pos),
            },
            RhiVertexAttributeDesc{
                .binding = 0,
                .semantic = "NORMAL0",
                .format = RhiVertexFormat::R32G32B32Float,
                .offset = offsetof(AssetManager::MeshVSInput, normal),
            },
            RhiVertexAttributeDesc{
                .binding = 0,
                .semantic = "TEXCOORD0",
                .format = RhiVertexFormat::R32G32Float,
                .offset = offsetof(AssetManager::MeshVSInput, uv),
            },
        };
        return ret;
    }

    static auto GetMeshVertexBindings() -> std::span<RhiVertexBindingDesc>
    {
        static auto ret = RhiVertexBindingDesc{
            .binding = 0,
            .stride = sizeof(AssetManager::MeshVSInput),
            .inputRate = RhiInputRate::PerVertex,
        };
        return std::span{&ret, 1};
    }

    static auto GetMeshPipelineColorTargetFormats() -> std::span<RhiTextureFormat>
    {
        static auto ret = RhiTextureFormat::B8G8R8A8_sRGB;
        return std::span{&ret, 1};
    }

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

    void Init();
    void Upload(RhiCmdList cmd);

    void Flush();

    auto DeclareTexture(std::string_view path) -> Texture;
    auto GetRhiSampledTextureView(Texture, RhiSampledTextureView &) -> bool;
    auto GetRhiSampledTextureView(Mesh, RhiSampledTextureView &) -> bool;

    auto DeclareMesh(std::string_view path) -> Mesh;
    auto DeclareStaticMesh(std::span<const char> vertexData, std::span<const uint16_t> indices) -> Mesh;

    void CmdBindMesh(RhiCmdList cmd, Mesh mesh);
    void CmdDrawMesh(RhiCmdList cmd, AssetManager::Mesh mesh);

  private:
    struct TextureData
    {
        std::string path;
        bool needsUpload;

        RhiTexture texture;
        RhiSampledTextureView textureView;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t channels = 0;
    };
    HandlePool<Texture, TextureData, 128> m_Textures;

    struct MeshData
    {
        bool isStatic;
        Path gltfPath;
        std::span<const char> vertexData;
        std::span<const uint16_t> indices;

        uint64_t vertexBufferOffset;
        uint64_t indexBufferOffset;
        uint32_t indexCount;

        AssetManager::Texture texture;

        bool needsUpload;
    };
    HandlePool<Mesh, MeshData, 128> m_Meshes;

    struct MeshPrimitiveData
    {
    };
    HandlePool<Mesh, MeshData, 128> m_MeshPrimitives;

    //

    struct GltfMeshUploadQueueEntry
    {
        char *path;
    };
    RegionAlloc m_GltfMeshUploadQueue;
};

} // namespace nyla