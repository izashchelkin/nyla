#include "nyla/spirview/spirview.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/containers/inline_string.h"
#include "nyla/commons/string.h"
#include <cstdint>
#include <cstring>
#include <span>
#include <sys/types.h>

#define spv_enable_utility_code
#if defined(__linux__)
#include <spirv/unified1/spirv.hpp>
#else
#include <spirv-headers/spirv.hpp>
#endif

namespace nyla
{

enum class SpvShaderStage
{
    Unknown,
    Vertex,
    Fragment,
    Compute,
};

struct SpvReflectResult
{
    SpvShaderStage entrypointStage;
};

auto SpvReflect(std::span<uint32_t> spirv, SpvReflectResult *result) -> bool;
auto SpvRewriteLocation(std::span<uint32_t> spirv, uint32_t id, uint32_t location);

namespace
{


auto ProcessOp(SpvReflectResult *result, spv::Op op, std::span<const uint32_t> args) -> bool
{
    uint32_t i = 0;

    bool strip = false;

    switch (op)
    {

    case spv::OpEntryPoint: {
        switch (spv::ExecutionModel(args[i++]))
        {
        case spv::ExecutionModel::ExecutionModelVertex:
            result->entrypointStage = SpvShaderStage::Vertex;
            break;
        case spv::ExecutionModel::ExecutionModelFragment:
            result->entrypointStage = SpvShaderStage::Fragment;
            break;
        default:
            break;
        }

        ++i; // entry point fn

        ParseStringLiteral([&args, &i]() -> uint32_t { return args[i++]; }, [](uint32_t ch) -> void {});

#if 0
        while (i < args.size())
            result->entrypointInterfaceIds.emplace_back(args[i++]);
#endif

        break;
    }

    case spv::OpExtension: {
        InlineString<32> name;
        ParseStringLiteral([&args, &i]() -> uint32_t { return args[i++]; },
                           [&name](uint32_t ch) -> void { name.AppendChar(ch); });

        if (name.StringView() == "SPV_GOOGLE_hlsl_functionality1")
            strip = true;
        if (name.StringView() == "SPV_GOOGLE_user_type")
            strip = true;

        break;
    }


    default:
        break;
    }

    return strip;
}

} // namespace

auto SpvReflect(std::span<uint32_t> spirv, SpvReflectResult *result) -> bool
{
    auto view = Spirview{spirv};
    for (auto it = view.begin(); it != view.end(); ++it)
    {
        if (ProcessOp(result, it.Op(), it.Operands()))
            it.MakeNop();
    }

    return true;
}

} // namespace nyla
  