#include "nyla/commons/shader.h"

#include <cstdint>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace
{

struct shader_cache_entry
{
    uint64_t guid;
    rhi_shader_stage stage;
    rhi_shader handle;
    const uint8_t *lastDataPtr;
};

struct shader_state
{
    inline_vec<shader_cache_entry, 64> entries;
};
shader_state *g_shader;

auto AsCode(byteview data) -> span<uint32_t>
{
    return span<uint32_t>{(uint32_t *)data.data, data.size / sizeof(uint32_t)};
}

void OnAssetChanged(uint64_t guid, byteview data, void *)
{
    for (uint64_t i = 0; i < g_shader->entries.size; ++i)
    {
        shader_cache_entry &e = g_shader->entries[i];
        if (e.guid != guid)
            continue;
        Rhi::ReloadShader(e.handle, AsCode(data));
        e.lastDataPtr = data.data;
    }
}

} // namespace

namespace Shader
{

void API Bootstrap()
{
    g_shader = &RegionAlloc::Alloc<shader_state>(RegionAlloc::g_BootstrapAlloc);
    AssetManager::Subscribe(OnAssetChanged, nullptr);
}

} // namespace Shader

auto API GetShader(uint64_t guid, rhi_shader_stage stage) -> rhi_shader
{
    for (uint64_t i = 0; i < g_shader->entries.size; ++i)
    {
        shader_cache_entry &e = g_shader->entries[i];
        if (e.guid == guid && e.stage == stage)
            return e.handle;
    }

    byteview data = AssetManager::Get(guid);
    rhi_shader handle = Rhi::CreateShader(rhi_shader_desc{
        .stage = stage,
        .code = AsCode(data),
    });
    InlineVec::Append(g_shader->entries, shader_cache_entry{
                                             .guid = guid,
                                             .stage = stage,
                                             .handle = handle,
                                             .lastDataPtr = data.data,
                                         });
    return handle;
}

} // namespace nyla
