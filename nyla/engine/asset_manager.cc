#include "nyla/engine/asset_manager.h"
#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/handle.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/log.h"
#include "nyla/commons/memory/region_alloc.h"
#include "nyla/commons/path.h"
#include "nyla/engine/engine.h"
#include "nyla/formats/gltf/gltf_parser.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_buffer.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_sampler.h"
#include "nyla/rhi/rhi_texture.h"
#include "third_party/stb/stb_image.h"
#include <cstdint>

namespace nyla
{

void AssetManager::Init()
{
    static bool inited = false;
    NYLA_ASSERT(!inited);
    inited = true;

    //

    auto addSampler = [this](SamplerType samplerType, RhiFilter filter, RhiSamplerAddressMode addressMode) -> void {
        RhiSampler sampler = g_Rhi.CreateSampler({
            .minFilter = filter,
            .magFilter = filter,
            .addressModeU = addressMode,
            .addressModeV = addressMode,
            .addressModeW = addressMode,
        });
    };
    addSampler(SamplerType::LinearClamp, RhiFilter::Linear, RhiSamplerAddressMode::ClampToEdge);
    addSampler(SamplerType::LinearRepeat, RhiFilter::Linear, RhiSamplerAddressMode::Repeat);
    addSampler(SamplerType::NearestClamp, RhiFilter::Nearest, RhiSamplerAddressMode::ClampToEdge);
    addSampler(SamplerType::NearestRepeat, RhiFilter::Nearest, RhiSamplerAddressMode::Repeat);
}

void AssetManager::Upload(RhiCmdList cmd)
{
    auto &uploadManager = g_Engine.GetUploadManager();

    for (uint32_t i = 0; i < m_Meshes.size(); ++i)
    {
        auto &slot = *(m_Meshes.begin() + i);
        if (!slot.used)
            continue;

        MeshData &meshData = slot.data;
        if (!meshData.needsUpload)
            continue;

        if (meshData.isStatic)
        {
            meshData.indexCount = meshData.indices.size();
            NYLA_ASSERT(meshData.indexCount % 3 == 0);

            char *uploadMemory =
                uploadManager.CmdCopyStaticIndices(cmd, meshData.indices.size_bytes(), meshData.indexBufferOffset);
            memcpy(uploadMemory, meshData.indices.data(), meshData.indices.size_bytes());

            uploadMemory =
                uploadManager.CmdCopyStaticVertices(cmd, meshData.vertexData.size_bytes(), meshData.vertexBufferOffset);
            memcpy(uploadMemory, meshData.vertexData.data(), meshData.vertexData.size_bytes());
        }
        else
        {
            RegionAlloc &transientAlloc = g_Engine.GetPerFrameAlloc();
            RegionAlloc scratchAlloc = transientAlloc.PushSubAlloc(16_KiB);

            NYLA_ASSERT(meshData.gltfPath.EndsWith(".gltf"));
            std::vector<std::byte> gltfData = g_Platform.ReadFile(meshData.gltfPath.StrView());
            std::vector<std::byte> binData =
                g_Platform.ReadFile(meshData.gltfPath.Clone(scratchAlloc).SetExtension(".bin").StrView());

            {
                GltfParser parser;
                parser.Init(&scratchAlloc, std::span{(char *)gltfData.data(), gltfData.size()},
                            std::span{(char *)binData.data(), binData.size()});
                NYLA_ASSERT(parser.Parse());

                for (const GltfImage &image : parser.GetImages())
                {
                }

                for (const GltfMesh &mesh : parser.GetMeshes())

                    for (const GltfMeshPrimitive &primitive : mesh.primitives)
                    {
                        {
                            GltfAccessor indices = parser.GetAccessor(primitive.indices);
                            NYLA_ASSERT(indices.type == GltfAccessorType::SCALAR);
                            NYLA_ASSERT(indices.componentType == GltfAccessorComponentType::UNSIGNED_SHORT);

                            GltfBufferView bufferView = parser.GetBufferView(indices.bufferView);

                            uint32_t bufferSize = indices.count * GetGltfAccessorSize(indices);
                            char *const uploadMemory =
                                uploadManager.CmdCopyStaticIndices(cmd, bufferSize, meshData.indexBufferOffset);

                            std::span<char> indicesData = parser.GetAccessorData(indices);
                            memcpy(uploadMemory, indicesData.data(), indicesData.size_bytes());

                            NYLA_ASSERT((indicesData.size_bytes() % sizeof(uint16_t)) == 0);
                            meshData.indexCount = indicesData.size_bytes() / sizeof(uint16_t);
                            NYLA_ASSERT(meshData.indexCount % 3 == 0);
                        }

                        {
                            auto &attrs = primitive.attributes;

                            uint32_t stride = 0;

                            auto alignStride = [&](GltfAccessor accessor) -> void {
                                const uint32_t componentSize = GetGltfAccessorComponentSize(accessor.componentType);
                                AlignUp(stride, componentSize);
                            };
                            auto countStride = [&](GltfAccessor accessor) -> uint32_t {
                                alignStride(accessor);
                                const uint32_t offset = stride;
                                stride += GetGltfAccessorSize(accessor);
                                return offset;
                            };

                            GltfAccessor pos;
                            NYLA_ASSERT(parser.FindAttributeAccessor(attrs, "POSITION", pos));
                            NYLA_ASSERT(pos.type == GltfAccessorType::VEC3);
                            NYLA_ASSERT(pos.componentType == GltfAccessorComponentType::FLOAT);
                            const uint32_t posOffset = countStride(pos);

                            GltfAccessor norm;
                            NYLA_ASSERT(parser.FindAttributeAccessor(attrs, "NORMAL", norm));
                            NYLA_ASSERT(norm.type == GltfAccessorType::VEC3);
                            NYLA_ASSERT(norm.componentType == GltfAccessorComponentType::FLOAT);
                            NYLA_ASSERT(norm.count == pos.count);
                            const uint32_t normOffset = countStride(norm);

                            GltfAccessor texCoord;
                            NYLA_ASSERT(parser.FindAttributeAccessor(attrs, "TEXCOORD_0", texCoord));
                            NYLA_ASSERT(texCoord.type == GltfAccessorType::VEC2);
                            NYLA_ASSERT(texCoord.componentType == GltfAccessorComponentType::FLOAT);
                            NYLA_ASSERT(texCoord.count == pos.count);
                            const uint32_t texCoordOffset = countStride(texCoord);

                            alignStride(pos);
                            const uint32_t bufferSize = stride * pos.count;

                            const auto posBufferView = parser.GetAccessorData(pos);
                            const auto normBufferView = parser.GetAccessorData(norm);
                            const auto texCoordBufferView = parser.GetAccessorData(texCoord);

                            char *const uploadMemory =
                                uploadManager.CmdCopyStaticVertices(cmd, bufferSize, meshData.vertexBufferOffset);

                            for (uint32_t i = 0; i < pos.count; ++i)
                            {
                                auto copyAttribute = [uploadMemory, i, stride](GltfAccessor accessor, uint32_t offset,
                                                                               char *byteBufferViewData) -> void {
                                    void *const dst = uploadMemory + static_cast<uint64_t>(i * stride + offset);
                                    const void *const src =
                                        byteBufferViewData + static_cast<uint64_t>(i * GetGltfAccessorSize(accessor));
                                    memcpy(dst, src, GetGltfAccessorSize(accessor));
                                };

                                copyAttribute(pos, posOffset, posBufferView.data());
                                copyAttribute(norm, normOffset, normBufferView.data());
                                copyAttribute(texCoord, texCoordOffset, texCoordBufferView.data());
                            }
                        }
                    }
            }
            transientAlloc.Pop(scratchAlloc.GetBase());
        }

        meshData.needsUpload = false;
    }

    for (uint32_t i = 0; i < m_Textures.size(); ++i)
    {
        auto &slot = *(m_Textures.begin() + i);
        if (!slot.used)
            continue;

        TextureData &textureAssetData = slot.data;
        if (!textureAssetData.needsUpload)
            continue;

        void *data = stbi_load(textureAssetData.path.c_str(), (int *)&textureAssetData.width,
                               (int *)&textureAssetData.height, (int *)&textureAssetData.channels, STBI_rgb_alpha);
        if (!data)
        {
            NYLA_LOG("stbi_load failed for '%s': %s", textureAssetData.path.data(), stbi_failure_reason());
            NYLA_ASSERT(false);
        }

        const RhiTexture texture = g_Rhi.CreateTexture(RhiTextureDesc{
            .width = textureAssetData.width,
            .height = textureAssetData.height,
            .memoryUsage = RhiMemoryUsage::GpuOnly,
            .usage = RhiTextureUsage::TransferDst | RhiTextureUsage::ShaderSampled,
            .format = RhiTextureFormat::R8G8B8A8_sRGB,
        });
        textureAssetData.texture = texture;

        const RhiSampledTextureView textureView = g_Rhi.CreateSampledTextureView(RhiTextureViewDesc{
            .texture = texture,
        });
        textureAssetData.textureView = textureView;

        g_Rhi.CmdTransitionTexture(cmd, texture, RhiTextureState::TransferDst);

        const uint32_t size = textureAssetData.width * textureAssetData.height * textureAssetData.channels;

        char *uploadMemory = g_Engine.GetUploadManager().CmdCopyTexture(cmd, texture, size);
        memcpy(uploadMemory, data, size);

        free(data);

        // TODO: this barrier does not need to be here
        g_Rhi.CmdTransitionTexture(cmd, texture, RhiTextureState::ShaderRead);

        NYLA_LOG("Uploading '%s'", (const char *)textureAssetData.path.data());

        textureAssetData.needsUpload = false;
    }
}

auto AssetManager::DeclareTexture(std::string_view path) -> Texture
{
    return m_Textures.Acquire(TextureData{
        .path = std::string{path},
        .needsUpload = true,
    });
}

auto AssetManager::GetRhiSampledTextureView(Texture texture, RhiSampledTextureView &out) -> bool
{
    const auto &data = m_Textures.ResolveData(texture);
    if (HandleIsSet(data.texture))
    {
        out = data.textureView;
        return true;
    }

    return false;
}

auto AssetManager::DeclareMesh(std::string_view path) -> Mesh
{
    return m_Meshes.Acquire(MeshData{
        .isStatic = false,
        .gltfPath = std::string{path},
        .needsUpload = true,
    });
}

auto AssetManager::DeclareStaticMesh(std::span<const char> vertexData, std::span<const uint16_t> indices) -> Mesh
{
    return m_Meshes.Acquire(MeshData{
        .isStatic = true,
        .vertexData = vertexData,
        .indices = indices,
        .needsUpload = true,
    });
}

void AssetManager::CmdBindMesh(RhiCmdList cmd, Mesh mesh)
{
    auto &uploadManager = g_Engine.GetUploadManager();

    const auto &meshData = m_Meshes.ResolveData(mesh);
    NYLA_ASSERT(!meshData.needsUpload);

    uploadManager.CmdBindStaticMeshVertexBuffer(cmd, meshData.vertexBufferOffset);
    uploadManager.CmdBindStaticMeshIndexBuffer(cmd, meshData.indexBufferOffset);
}

void AssetManager::CmdDrawMesh(RhiCmdList cmd, AssetManager::Mesh mesh)
{
    const auto &meshData = m_Meshes.ResolveData(mesh);
    NYLA_ASSERT(!meshData.needsUpload);

    g_Rhi.CmdDrawIndexed(cmd, meshData.indexCount, 0, 1, 0, 0);
}

} // namespace nyla