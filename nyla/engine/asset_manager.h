#pragma once

#include "nyla/commons/handle.h"
#include "nyla/commons/handle_pool.h"
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
    auto DeclareMesh(std::string_view path) -> Mesh;

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
        std::string path;
        bool needsUpload;
    };
    HandlePool<Mesh, MeshData, 128> m_Meshes;

    struct MeshPrimitiveData
    {
        bool needsUpload;
    };
    HandlePool<Mesh, MeshData, 128> m_MeshPrimitives;
};

} // namespace nyla