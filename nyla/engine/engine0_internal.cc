#include "nyla/engine/engine0_internal.h"

#include "nyla/platform/platform.h"
#include "nyla/rhi/rhi.h"

#include <cstdint>
#include <format>
#include <sys/types.h>
#include <vector>

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

    std::vector<std::byte> code = Platform::ReadFile(path);
    const auto spirv = std::span{reinterpret_cast<uint32_t *>(code.data()), code.size() / 4};

    RhiShader shader = Rhi::CreateShader(RhiShaderDesc{
        .stage = stage,
        .code = spirv,
    });
    return shader;
}

} // namespace nyla::engine0_internal