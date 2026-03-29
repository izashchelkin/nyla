#include "nyla/engine/asset_manager.h"
#include "nyla/alloc/region_alloc.h"
#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/handle.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/log.h"
#include "nyla/commons/path.h"
#include "nyla/engine/engine.h"
#include "nyla/engine/gpu_upload_manager.h"
#include "nyla/formats/gltf/gltf.h"
#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"
#include "third_party/stb/stb_image.h"
#include <cstdint>

namespace nyla
{

// TODO: we need a real asset system!

namespace
{

struct TextureData
{
    bool isStatic;
    std::string path;
    bool needsUpload;

    RhiTexture texture;
    RhiSampledTextureView textureView;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
};
HandlePool<AssetManager::Texture, TextureData, 128> g_Textures;

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
HandlePool<AssetManager::Mesh, MeshData, 128> g_Meshes;

} // namespace

auto AssetManager::GetMeshVertexAttributes() -> std::span<RhiVertexAttributeDesc>
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

auto AssetManager::GetMeshVertexBindings() -> std::span<RhiVertexBindingDesc>
{
    static auto ret = RhiVertexBindingDesc{
        .binding = 0,
        .stride = sizeof(AssetManager::MeshVSInput),
        .inputRate = RhiInputRate::PerVertex,
    };
    return std::span{&ret, 1};
}

auto AssetManager::GetMeshPipelineColorTargetFormats() -> std::span<RhiTextureFormat>
{
    static auto ret = RhiTextureFormat::B8G8R8A8_sRGB;
    return std::span{&ret, 1};
}

void AssetManager::Init()
{
    static bool inited = false;
    NYLA_ASSERT(!inited);
    inited = true;

    //

    auto addSampler = [](SamplerType samplerType, RhiFilter filter, RhiSamplerAddressMode addressMode) -> void {
        RhiSampler sampler = Rhi::CreateSampler({
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

void AssetManager::Flush()
{
    for (auto &slot : g_Textures)
    {
        if (slot.used)
        {
            slot.data.needsUpload = true;

            Rhi::DestroySampledTextureView(slot.data.textureView);
            Rhi::DestroyTexture(slot.data.texture);
        }
    }

    for (auto &slot : g_Meshes)
    {
        if (slot.used)
        {
            slot.data.needsUpload = true;
        }
    }
}

void AssetManager::Upload(RhiCmdList cmd)
{
    for (uint32_t i = 0; i < g_Meshes.size(); ++i)
    {
        auto &slot = *(g_Meshes.begin() + i);
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
                GpuUploadManager::CmdCopyStaticIndices(cmd, meshData.indices.size_bytes(), meshData.indexBufferOffset);
            memcpy(uploadMemory, meshData.indices.data(), meshData.indices.size_bytes());

            uploadMemory = GpuUploadManager::CmdCopyStaticVertices(cmd, meshData.vertexData.size_bytes(),
                                                                   meshData.vertexBufferOffset);
            memcpy(uploadMemory, meshData.vertexData.data(), meshData.vertexData.size_bytes());
        }
        else
        {
            auto &transientAlloc = Engine::GetPerFrameAlloc();
            RegionAlloc scratchAlloc = transientAlloc.PushSubAlloc(16_KiB);

            NYLA_ASSERT(meshData.gltfPath.EndsWith(".gltf"));
            std::vector<std::byte> gltfData = Platform::ReadFile(meshData.gltfPath.StrView());
            std::vector<std::byte> binData =
                Platform::ReadFile(meshData.gltfPath.Clone(scratchAlloc).SetExtension(".bin").StrView());

            {
                GltfParser parser;
                parser.Init(&scratchAlloc, std::span{(char *)gltfData.data(), gltfData.size()},
                            std::span{(char *)binData.data(), binData.size()});
                NYLA_ASSERT(parser.Parse());

                NYLA_ASSERT(parser.GetImages().size() == 1);

                {
                    GltfImage image = parser.GetImages().front();
                    Path path = meshData.gltfPath.Clone(scratchAlloc).PopBack().Append(image.uri);
                    meshData.texture = DeclareTexture(path.StrView());
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
                                GpuUploadManager::CmdCopyStaticIndices(cmd, bufferSize, meshData.indexBufferOffset);

                            std::span<char> indicesData = parser.GetAccessorData(indices);

#if 0
                            { // conversion
                                std::span<uint16_t> indices{(uint16_t *)indicesData.data(),
                                                            indicesData.size_bytes() / sizeof(uint16_t)};
                                for (uint32_t i = 0; i < indices.size(); i += 3)
                                {
                                    std::swap(indices[i + 1], indices[i + 2]);
                                }
                            }
#endif

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
                                GpuUploadManager::CmdCopyStaticVertices(cmd, bufferSize, meshData.vertexBufferOffset);

                            for (uint32_t i = 0; i < pos.count; ++i)
                            {

#if 0
                                { // conversion
                                    auto *thisPos =
                                        (float3 *)(posBufferView.data() + size_t(GetGltfAccessorSize(pos) * i));
                                    (*thisPos)[2] = -(*thisPos)[2];

                                    auto *thisNormal =
                                        (float3 *)(normBufferView.data() + size_t(GetGltfAccessorSize(norm) * i));
                                    (*thisNormal)[2] = -(*thisNormal)[2];
                                }
#endif

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

    for (uint32_t i = 0; i < g_Textures.size(); ++i)
    {
        auto &slot = *(g_Textures.begin() + i);
        if (!slot.used)
            continue;

        TextureData &textureAssetData = slot.data;
        if (!textureAssetData.needsUpload)
            continue;

        // stbi_set_flip_vertically_on_load(true);
        void *data = stbi_load(textureAssetData.path.c_str(), (int *)&textureAssetData.width,
                               (int *)&textureAssetData.height, (int *)&textureAssetData.channels, STBI_rgb_alpha);

        if (textureAssetData.channels != 4) // alpha missing?
            textureAssetData.channels = 4;

        if (!data)
        {
            NYLA_LOG("stbi_load failed for '%s': %s", textureAssetData.path.data(), stbi_failure_reason());
            NYLA_ASSERT(false);
        }

        const RhiTexture texture = Rhi::CreateTexture(RhiTextureDesc{
            .width = textureAssetData.width,
            .height = textureAssetData.height,
            .memoryUsage = RhiMemoryUsage::GpuOnly,
            .usage = RhiTextureUsage::TransferDst | RhiTextureUsage::ShaderSampled,
            .format = RhiTextureFormat::R8G8B8A8_sRGB,
        });
        textureAssetData.texture = texture;

        const RhiSampledTextureView textureView = Rhi::CreateSampledTextureView(RhiTextureViewDesc{
            .texture = texture,
        });
        textureAssetData.textureView = textureView;

        Rhi::CmdTransitionTexture(cmd, texture, RhiTextureState::TransferDst);

        const uint32_t size = textureAssetData.width * textureAssetData.height * textureAssetData.channels;

        char *uploadMemory = GpuUploadManager::CmdCopyTexture(cmd, texture, size);
        memcpy(uploadMemory, data, size);

        free(data);

        // TODO: this barrier does not need to be here
        Rhi::CmdTransitionTexture(cmd, texture, RhiTextureState::ShaderRead);

        NYLA_LOG("Uploading '%s'", (const char *)textureAssetData.path.data());

        textureAssetData.needsUpload = false;
    }
}

auto AssetManager::DeclareTexture(std::string_view path) -> Texture
{
    return g_Textures.Acquire(TextureData{
        .path = std::string{path},
        .needsUpload = true,
    });
}

auto AssetManager::GetRhiSampledTextureView(Texture texture, RhiSampledTextureView &out) -> bool
{
    const auto &data = g_Textures.ResolveData(texture);
    if (HandleIsSet(data.texture))
    {
        out = data.textureView;
        return true;
    }

    return false;
}

auto AssetManager::GetRhiSampledTextureView(Mesh mesh, RhiSampledTextureView &out) -> bool
{
    const auto &data = g_Meshes.ResolveData(mesh);
    if (HandleIsSet(data.texture))
        return GetRhiSampledTextureView(data.texture, out);
    else
        return false;
}

auto AssetManager::DeclareMesh(std::string_view path) -> Mesh
{
    return g_Meshes.Acquire(MeshData{
        .isStatic = false,
        .gltfPath = Engine::GetPermanentAlloc().PushPath(path),
        .needsUpload = true,
    });
}

auto AssetManager::DeclareStaticMesh(std::span<const char> vertexData, std::span<const uint16_t> indices) -> Mesh
{
    return g_Meshes.Acquire(MeshData{
        .isStatic = true,
        .vertexData = vertexData,
        .indices = indices,
        .needsUpload = true,
    });
}

void AssetManager::CmdBindMesh(RhiCmdList cmd, Mesh mesh)
{
    const auto &meshData = g_Meshes.ResolveData(mesh);
    NYLA_ASSERT(!meshData.needsUpload);

    GpuUploadManager::CmdBindStaticMeshVertexBuffer(cmd, meshData.vertexBufferOffset);
    GpuUploadManager::CmdBindStaticMeshIndexBuffer(cmd, meshData.indexBufferOffset);
}

void AssetManager::CmdDrawMesh(RhiCmdList cmd, AssetManager::Mesh mesh)
{
    const auto &meshData = g_Meshes.ResolveData(mesh);
    NYLA_ASSERT(!meshData.needsUpload);

    Rhi::CmdDrawIndexed(cmd, meshData.indexCount, 0, 1, 0, 0);
}

} // namespace nyla