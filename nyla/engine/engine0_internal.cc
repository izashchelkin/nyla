#include "nyla/engine/engine0_internal.h"

#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_shader.h"
#include <cstdint>
#include <format>
#include <sys/types.h>

namespace nyla::engine0_internal
{

auto GetShader(const char *name, RhiShaderStage stage) -> RhiShader
{
#if defined(__linux__) // TODO : deal with this please
    const std::string path = std::format("/home/izashchelkin/nyla/nyla/shaders/build/{}.hlsl.spv", name);
#else
    const std::string path = std::format("D:\\nyla\\nyla\\shaders\\build\\{}.hlsl.spv", name);
#endif
    // TODO: directory watch
    // PlatformFsWatchFile(path);

    std::vector<std::byte> code = g_Platform->ReadFile(path);
    const auto spirv = std::span{reinterpret_cast<uint32_t *>(code.data()), code.size() / 4};

    RhiShader shader = g_Rhi.CreateShader(RhiShaderDesc{
        .stage = stage,
        .code = spirv,
    });
    return shader;
}

} // namespace nyla::engine0_internal