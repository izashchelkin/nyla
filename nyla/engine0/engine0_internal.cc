#include "nyla/engine0/engine0_internal.h"

#include "absl/log/check.h"
#include "nyla/commons/os/readfile.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/spirview/spirview.h"
#include <cstdint>
#include <format>
#include <sys/types.h>
#include <unistd.h>

namespace nyla::engine0_internal
{

Engine0Handles e0Handles;

auto GetShader(std::string_view name, RhiShaderStage stage) -> RhiShader
{
    const std::string path = std::format("nyla/shaders/build/{}.hlsl.spv", name);
    const std::vector<std::byte> code = ReadFile(path);
    const auto spirv = std::span{reinterpret_cast<const uint32_t *>(code.data()), code.size() / 4};

    SpirviewReflectResult result{};
    SpirviewReflect(spirv, &result);

    switch (result.stage)
    {
    case SpirviewShaderStage::Vertex: {
        CHECK_EQ(stage, RhiShaderStage::Vertex);
        break;
    }
    case SpirviewShaderStage::Fragment: {
        CHECK_EQ(stage, RhiShaderStage::Pixel);
        break;
    }
    default: {
        CHECK(false);
    }
    }

    RhiShader shader = RhiCreateShader(RhiShaderDesc{
        .spirv = spirv,
    });
    return shader;
}

} // namespace nyla::engine0_internal