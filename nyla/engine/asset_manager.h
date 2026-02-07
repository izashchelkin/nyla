#pragma once

#include "nyla/commons/handle.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/math/vec.h"
#include "nyla/commons/memory/region_alloc.h"
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

    auto DeclareTexture(std::string_view path) -> Texture;
    auto GetRhiTexture(Texture, RhiTexture &) -> bool;

    auto DeclareMesh(std::string_view path) -> Mesh;
    auto DeclareStaticMesh(std::span<const char> vertexData, std::span<const uint32_t> indices) -> Mesh;

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
        std::string path;
        std::span<const char> vertexData;
        std::span<const uint32_t> indices;

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