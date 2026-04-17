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

} // namespace nyla