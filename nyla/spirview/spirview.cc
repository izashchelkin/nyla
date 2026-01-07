#include "nyla/spirview/spirview.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/containers/inline_string.h"
#include "nyla/commons/string.h"
#include <cstdint>
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

namespace
{

auto ParseStringLiteral(auto supplier, auto consumer)
{
    for (;;)
    {
        uint32_t word = supplier();

        uint32_t ch;
        ch = (word >> 0) & 0xFF;
        if (ch)
            consumer(ch);
        else
            break;

        ch = (word >> 8) & 0xFF;
        if (ch)
            consumer(ch);
        else
            break;

        ch = (word >> 16) & 0xFF;
        if (ch)
            consumer(ch);
        else
            break;

        ch = (word >> 24) & 0xFF;
        if (ch)
            consumer(ch);
        else
            break;
    }
}

auto ProcessOp(SpirviewReflectResult *result, spv::Op op, std::span<const uint32_t> args) -> bool
{
    uint32_t i = 0;

    bool keep = true;

    switch (op)
    {

    case spv::OpEntryPoint: {
        switch (spv::ExecutionModel(args[i++]))
        {
        case spv::ExecutionModel::ExecutionModelVertex:
            result->stage = SpirviewShaderStage::Vertex;
            break;
        case spv::ExecutionModel::ExecutionModelFragment:
            result->stage = SpirviewShaderStage::Fragment;
            break;
        default:
            break;
        }

        break;
    }

    case spv::OpDecorate: {
        spv::Id targetId = args[i++];

        auto decoration = spv::Decoration(args[i++]);
        switch (decoration)
        {
        case spv::Decoration::DecorationLocation: {
            result->locations.emplace_back(SpirviewReflectResult::IdLocation{.id = targetId, .location = args[i++]});
            break;
        }

        default:
            break;
        }

        break;
    }

    case spv::OpExtension: {
        InlineString<32> name;
        ParseStringLiteral([&args, &i]() -> uint32_t { return args[i++]; },
                           [&name](uint32_t ch) -> void { name.AppendChar(ch); });

        if (name.StringView() == "SPV_GOOGLE_hlsl_functionality1")
            keep = false;
        if (name.StringView() == "SPV_GOOGLE_user_type")
            keep = false;

        break;
    }

    case spv::OpDecorateString: {

        spv::Id targetId = args[i++];

        auto decoration = spv::Decoration(args[i++]);
        switch (decoration)
        {

        case spv::Decoration::DecorationUserTypeGOOGLE: {
            keep = false;
            break;
        }

        case spv::Decoration::DecorationUserSemantic: {
            keep = false;

            decltype(SpirviewReflectResult::IdSemantic::semantic) semantic;
            ParseStringLiteral([&args, &i]() -> uint32_t { return args[i++]; },
                               [&semantic](uint32_t ch) -> void { semantic.AppendChar(AsciiChrToUpper(ch)); });

            result->semantics.emplace_back(SpirviewReflectResult::IdSemantic{.id = targetId, .semantic = semantic});
            break;
        }

        default:
            break;
        }

        break;
    }

    default:
        break;
    }

    return keep;
}

} // namespace

auto SpirviewReflect(std::span<const uint32_t> spirv, SpirviewReflectResult *result) -> bool
{
    NYLA_ASSERT(spirv[0] == spv::MagicNumber);

    const uint32_t headerVersionMinor = (spirv[1] >> 8) & 0xFF;
    const uint32_t headerVersionMajor = (spirv[1] >> 16) & 0xFF;

    const uint32_t headerGeneratorMagic = spirv[2];
    const uint32_t headerIdBound = spirv[3];

    for (uint33_t i = 0; i < 5; ++i)
        result->outSpirv.emplace_back(spirv[i]);

    for (uint32_t i = 5; i < spirv.size();)
    {
        const uint32_t word = spirv[i];

        const auto op = spv::Op(word & spv::OpCodeMask);
        const uint16_t wordCount = word >> spv::WordCountShift;
        NYLA_ASSERT(wordCount);

        const bool keep = ProcessOp(result, op, std::span{spirv.data() + i + 1, static_cast<uint16_t>(wordCount - 1)});
        if (keep)
        {
            for (uint32_t j = 0; j < wordCount; ++j)
                result->outSpirv.emplace_back(spirv[i + j]);
        }

        i += wordCount;
    }

    return true;
}

} // namespace nyla