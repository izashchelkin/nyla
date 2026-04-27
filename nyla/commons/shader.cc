#include "nyla/commons/shader.h"

#include "nyla/commons/asset_manager.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

auto API GetShader(uint64_t guid, rhi_shader_stage stage) -> rhi_shader
{
    byteview data = AssetManager::Get(guid);

    return Rhi::CreateShader(rhi_shader_desc{
        .stage = stage,
        .code = span<uint32_t>{(uint32_t *)data.data, data.size / sizeof(uint32_t)},
    });
}

} // namespace nyla
