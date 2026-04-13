#include <cstdint>

#include "nyla/commons/align.h"
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/engine.h"
#include "nyla/commons/gltf/gltf.h"
#include "nyla/commons/gpu_upload_manager.h"
#include "nyla/commons/handle.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/log.h"
#include "nyla/commons/path.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

// TODO: we need a real asset system!

namespace
{

struct TextureData
{
    bool isStatic;
    Str path;
    bool needsUpload;

    RhiTexture texture;
    RhiSampledTextureView textureView;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
};
HandlePool<AssetManager::Texture, TextureData, 128> g_Textures;

struct MeshData
{
    bool isStatic;
    Path gltfPath;
    Span<const char> vertexData;
    Span<const uint16_t> indices;

    uint64_t vertexBufferOffset;
    uint64_t indexBufferOffset;
    uint32_t indexCount;

    AssetManager::Texture texture;

    bool needsUpload;
};
HandlePool<AssetManager::Mesh, MeshData, 128> g_Meshes;

} // namespace

auto AssetManager::GetMeshVertexAttributes() -> Span<RhiVertexAttributeDesc>
{
    static Array<RhiVertexAttributeDesc, 3> ret{
        RhiVertexAttributeDesc{
            .binding = 0,
            .semantic = AsInlineStr<16>(AsStr("POSITION0")),
            .format = RhiVertexFormat::R32G32B32Float,
            .offset = offsetof(AssetManager::MeshVSInput, pos),
        },
        RhiVertexAttributeDesc{
            .binding = 0,
            .semantic = AsInlineStr<16>(AsStr("NORMAL0")),
            .format = RhiVertexFormat::R32G32B32Float,
            .offset = offsetof(AssetManager::MeshVSInput, normal),
        },
        RhiVertexAttributeDesc{
            .binding = 0,
            .semantic = AsInlineStr<16>(AsStr("TEXCOORD0")),
            .format = RhiVertexFormat::R32G32Float,
            .offset = offsetof(AssetManager::MeshVSInput, uv),
        },
    };
    return ret.GetSpan();
}

auto AssetManager::GetMeshVertexBindings() -> Span<RhiVertexBindingDesc>
{
    static auto ret = RhiVertexBindingDesc{
        .binding = 0,
        .stride = sizeof(AssetManager::MeshVSInput),
        .inputRate = RhiInputRate::PerVertex,
    };
    return Span{&ret, 1};
}

auto AssetManager::GetMeshPipelineColorTargetFormats() -> Span<RhiTextureFormat>
{
    static auto ret = RhiTextureFormat::B8G8R8A8_sRGB;
    return Span{&ret, 1};
}

void AssetManager::Init()
{
    static bool inited = false;
    ASSERT(!inited);
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
    Engine::GetPerFrameAlloc().VoidScope(1 << 23, [&](auto &alloc) {
        for (uint32_t i = 0; i < g_Meshes.Size(); ++i)
        {
            auto &slot = *(g_Meshes.begin() + i);
            if (!slot.used)
                continue;

            MeshData &meshData = slot.data;
            if (!meshData.needsUpload)
                continue;

            if (meshData.isStatic)
            {
                meshData.indexCount = meshData.indices.Size();
                ASSERT(meshData.indexCount % 3 == 0);

                char *uploadMemory = GpuUploadManager::CmdCopyStaticIndices(cmd, meshData.indices.SizeBytes(),
                                                                            meshData.indexBufferOffset);
                memcpy(uploadMemory, meshData.indices.Data(), meshData.indices.SizeBytes());

                uploadMemory = GpuUploadManager::CmdCopyStaticVertices(cmd, meshData.vertexData.SizeBytes(),
                                                                       meshData.vertexBufferOffset);
                memcpy(uploadMemory, meshData.vertexData.Data(), meshData.vertexData.SizeBytes());
            }
            else
            {
                ASSERT(meshData.gltfPath.EndsWith(AsStr(".gltf")));
                Span gltfData = Platform::FileRead(meshData.gltfPath.GetStr());
                Span binData = Platform::FileRead(
                    alloc, ClonePath(alloc, meshData.gltfPath).SetExtension(".bin").GetStr(), 1 << 20);

                {
                    GltfParser parser;
                    parser.Init(&alloc, Span{(char *)gltfData.Data(), gltfData.Size()},
                                Span{(char *)binData.Data(), binData.Size()});
                    ASSERT(parser.Parse());

                    ASSERT(parser.GetImages().Size() == 1);

                    {
                        GltfImage image = parser.GetImages().Front();
                        Path path = alloc.ClonePath(alloc, meshData.gltfPath).PopBack().Append(image.uri);
                        meshData.texture = DeclareTexture(path.GetStr());
                    }

                    for (const GltfMesh &mesh : parser.GetMeshes())

                        for (const GltfMeshPrimitive &primitive : mesh.primitives)
                        {
                            {
                                GltfAccessor indices = parser.GetAccessor(primitive.indices);
                                ASSERT(indices.type == GltfAccessorType::SCALAR);
                                ASSERT(indices.componentType == GltfAccessorComponentType::UNSIGNED_SHORT);

                                GltfBufferView bufferView = parser.GetBufferView(indices.bufferView);

                                uint32_t bufferSize = indices.count * GetGltfAccessorSize(indices);
                                char *const uploadMemory =
                                    GpuUploadManager::CmdCopyStaticIndices(cmd, bufferSize, meshData.indexBufferOffset);

                                Span<char> indicesData = parser.GetAccessorData(indices);

#if 0
                            { // conversion
                                Span<uint16_t> indices{(uint16_t *)indicesData.Data(),
                                                            indicesData.SizeBytes() / sizeof(uint16_t)};
                                for (uint32_t i = 0; i < indices.Size(); i += 3)
                                {
                                    std::swap(indices[i + 1], indices[i + 2]);
                                }
                            }
#endif

                                memcpy(uploadMemory, indicesData.Data(), indicesData.SizeBytes());

                                ASSERT((indicesData.SizeBytes() % sizeof(uint16_t)) == 0);
                                meshData.indexCount = indicesData.SizeBytes() / sizeof(uint16_t);
                                ASSERT(meshData.indexCount % 3 == 0);
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
                                ASSERT(parser.FindAttributeAccessor(attrs, AsStr("POSITION"), pos));
                                ASSERT(pos.type == GltfAccessorType::VEC3);
                                ASSERT(pos.componentType == GltfAccessorComponentType::FLOAT);
                                const uint32_t posOffset = countStride(pos);

                                GltfAccessor norm;
                                ASSERT(parser.FindAttributeAccessor(attrs, AsStr("NORMAL"), norm));
                                ASSERT(norm.type == GltfAccessorType::VEC3);
                                ASSERT(norm.componentType == GltfAccessorComponentType::FLOAT);
                                ASSERT(norm.count == pos.count);
                                const uint32_t normOffset = countStride(norm);

                                GltfAccessor texCoord;
                                ASSERT(parser.FindAttributeAccessor(attrs, AsStr("TEXCOORD_0"), texCoord));
                                ASSERT(texCoord.type == GltfAccessorType::VEC2);
                                ASSERT(texCoord.componentType == GltfAccessorComponentType::FLOAT);
                                ASSERT(texCoord.count == pos.count);
                                const uint32_t texCoordOffset = countStride(texCoord);

                                alignStride(pos);
                                const uint32_t bufferSize = stride * pos.count;

                                const auto posBufferView = parser.GetAccessorData(pos);
                                const auto normBufferView = parser.GetAccessorData(norm);
                                const auto texCoordBufferView = parser.GetAccessorData(texCoord);

                                char *const uploadMemory = GpuUploadManager::CmdCopyStaticVertices(
                                    cmd, bufferSize, meshData.vertexBufferOffset);

                                for (uint32_t i = 0; i < pos.count; ++i)
                                {

#if 0
                                { // conversion
                                    auto *thisPos =
                                        (float3 *)(posBufferView.Data() + size_t(GetGltfAccessorSize(pos) * i));
                                    (*thisPos)[2] = -(*thisPos)[2];

                                    auto *thisNormal =
                                        (float3 *)(normBufferView.Data() + size_t(GetGltfAccessorSize(norm) * i));
                                    (*thisNormal)[2] = -(*thisNormal)[2];
                                }
#endif

                                    auto copyAttribute = [uploadMemory, i, stride](GltfAccessor accessor,
                                                                                   uint32_t offset,
                                                                                   char *byteBufferViewData) -> void {
                                        void *const dst = uploadMemory + static_cast<uint64_t>(i * stride + offset);
                                        const void *const src =
                                            byteBufferViewData +
                                            static_cast<uint64_t>(i * GetGltfAccessorSize(accessor));
                                        memcpy(dst, src, GetGltfAccessorSize(accessor));
                                    };

                                    copyAttribute(pos, posOffset, posBufferView.Data());
                                    copyAttribute(norm, normOffset, normBufferView.Data());
                                    copyAttribute(texCoord, texCoordOffset, texCoordBufferView.Data());
                                }
                            }
                        }
                }
            }

            meshData.needsUpload = false;
            alloc.Reset();
        }

        for (uint32_t i = 0; i < g_Textures.Size(); ++i)
        {
            auto &slot = *(g_Textures.begin() + i);
            if (!slot.used)
                continue;

            TextureData &textureAssetData = slot.data;
            if (!textureAssetData.needsUpload)
                continue;

            // stbi_set_flip_vertically_on_load(true);

            void *data = nullptr;
#if 0
                stbi_load(textureAssetData.path.c_str(), (int *)&textureAssetData.width,
                                   (int *)&textureAssetData.height, (int *)&textureAssetData.channels, STBI_rgb_alpha);
#endif

            if (textureAssetData.channels != 4) // alpha missing?
                textureAssetData.channels = 4;

            if (!data)
            {
                // LOG("stbi_load failed for '%s': %s", textureAssetData.path.Data(), stbi_failure_reason());
                ASSERT(false);
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

            LOG("Uploading '%s'", (const char *)textureAssetData.path.Data());

            textureAssetData.needsUpload = false;
        }
    });
}

auto AssetManager::DeclareTexture(Str path) -> Texture
{
    return g_Textures.Acquire(TextureData{
        .path = path,
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

auto AssetManager::DeclareMesh(Str path) -> Mesh
{
    return g_Meshes.Acquire(MeshData{
        .isStatic = false,
        .gltfPath = CreatePath(Engine::GetPermanentAlloc(), path),
        .needsUpload = true,
    });
}

auto AssetManager::DeclareStaticMesh(Span<const char> vertexData, Span<const uint16_t> indices) -> Mesh
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
    ASSERT(!meshData.needsUpload);

    GpuUploadManager::CmdBindStaticMeshVertexBuffer(cmd, meshData.vertexBufferOffset);
    GpuUploadManager::CmdBindStaticMeshIndexBuffer(cmd, meshData.indexBufferOffset);
}

void AssetManager::CmdDrawMesh(RhiCmdList cmd, AssetManager::Mesh mesh)
{
    const auto &meshData = g_Meshes.ResolveData(mesh);
    ASSERT(!meshData.needsUpload);

    Rhi::CmdDrawIndexed(cmd, meshData.indexCount, 0, 1, 0, 0);
}

} // namespace nyla