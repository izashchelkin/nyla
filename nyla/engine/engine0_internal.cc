#include "nyla/engine/engine0_internal.h"

#include "nyla/commons/assert.h"
#include "nyla/commons/os/readfile.h"
#include "nyla/rhi/rhi.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/spirview/spirview.h"
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

    const std::vector<std::byte> code = ReadFile(path);
    const auto spirv = std::span{reinterpret_cast<const uint32_t *>(code.data()), code.size() / 4};

    SpirviewReflectResult result{};
    SpirviewReflect(spirv, &result);

    switch (result.stage)
    {
    case SpirviewShaderStage::Vertex: {
        NYLA_ASSERT(stage == RhiShaderStage::Vertex);
        break;
    }
    case SpirviewShaderStage::Fragment: {
        NYLA_ASSERT(stage == RhiShaderStage::Pixel);
        break;
    }
    default: {
        NYLA_ASSERT(false);
    }
    }

    RhiShader shader = g_Rhi->CreateShader(RhiShaderDesc{
        .spirv = spirv,
    });
    return shader;
}

} // namespace nyla::engine0_internal