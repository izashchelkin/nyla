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
};

struct shader_manager
{
    inline_vec<shader_cache_entry, 64> entries;
};
shader_manager *manager;

void OnAssetChanged(uint64_t guid, byteview data, void *)
{
    for (uint64_t i = 0; i < manager->entries.size; ++i)
    {
        shader_cache_entry &e = manager->entries[i];
        if (e.guid == guid)
        {
            Rhi::ReloadShader(e.handle, Span::Cast<uint32_t>(data));
            break;
        }
    }
}

} // namespace

namespace Shader
{

void API Bootstrap()
{
    manager = &RegionAlloc::Alloc<shader_manager>(RegionAlloc::g_BootstrapAlloc);
    AssetManager::Subscribe(OnAssetChanged, nullptr);
}

} // namespace Shader

auto API GetShader(uint64_t guid, rhi_shader_stage stage) -> rhi_shader
{
    for (uint64_t i = 0; i < manager->entries.size; ++i)
    {
        shader_cache_entry &e = manager->entries[i];
        if (e.guid == guid && e.stage == stage)
            return e.handle;
    }

    byteview data = AssetManager::Get(guid);
    rhi_shader handle = Rhi::CreateShader(rhi_shader_desc{
        .stage = stage,
        .code = AsCode(data),
    });
    InlineVec::Append(manager->entries, shader_cache_entry{
                                            .guid = guid,
                                            .stage = stage,
                                            .handle = handle,
                                        });
    return handle;
}

} // namespace nyla