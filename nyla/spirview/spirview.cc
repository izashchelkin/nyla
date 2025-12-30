#include "nyla/spirview/spirview.h"
#include "nyla/commons/assert.h"
#include "nyla/rhi/rhi_shader.h"
#include <array>
#include <cstdint>
#include <span>
#include <variant>

#define spv_enable_utility_code
#if defined(__linux__)
#include <spirv/unified1/spirv.hpp>
#else
#include <spirv-headers/spirv.hpp>
#endif

namespace nyla
{

namespace spirview_internal
{

struct SpirvTypeIdState
{
    uint32_t typeWidth = (uint32_t)-1;
    uint32_t typeSignedness = (uint32_t)-1;
    uint32_t block = (uint32_t)-1;
};

struct SpirvVarIdState
{
    uint32_t varType = (uint32_t)-1;
    spv::StorageClass storageClass = spv::StorageClass::StorageClassMax;
    uint32_t set = (uint32_t)-1;
    uint32_t binding = (uint32_t)-1;
    uint32_t location = (uint32_t)-1;
};

using SpirvIdState = std::variant<std::monostate, SpirvTypeIdState, SpirvVarIdState>;

struct SpirvParserState
{
    spv::ExecutionModel executionModel;
    std::array<SpirvIdState, 1024> perId;
};

template <typename T> auto SpirvIdStateGetOrCreate(SpirvParserState &parser, uint32_t id) -> T &
{
    auto &variant = parser.perId[id];
    if (std::holds_alternative<std::monostate>(variant))
    {
        variant = T{};
    }

    return std::get<T>(variant);
}

auto GetVar(SpirvParserState &parser, uint32_t id) -> auto &
{
    return SpirvIdStateGetOrCreate<SpirvVarIdState>(parser, id);
}

auto GetType(SpirvParserState &parser, uint32_t id) -> auto &
{
    return SpirvIdStateGetOrCreate<SpirvTypeIdState>(parser, id);
}

void ProcessOp(SpirvParserState &parser, spv::Op op, std::span<const uint32_t> args)
{
    uint32_t i = 0;

    switch (op)
    {

    case spv::OpEntryPoint: {
        parser.executionModel = spv::ExecutionModel(args[i++]);

        break;
    }

    case spv::OpDecorate: {
        spv::Id targetId = args[i++];

        auto decoration = spv::Decoration(args[i++]);
        switch (decoration)
        {
        case spv::Decoration::DecorationDescriptorSet: {
            auto &var = GetVar(parser, targetId);
            var.set = args[i++];
            break;
        }
        case spv::Decoration::DecorationBinding: {
            auto &var = GetVar(parser, targetId);
            var.binding = args[i++];
            break;
        }
        case spv::Decoration::DecorationLocation: {
            auto &var = GetVar(parser, targetId);
            var.location = args[i++];
            break;
        }
        case spv::Decoration::DecorationBlock: {
            auto &type = GetType(parser, targetId);
            type.block = true;
            break;
        }
        }

        break;
    }

    case spv::OpVariable: {
        spv::Id resultType = args[i++];
        spv::Id resultId = args[i++];
        auto &var = GetVar(parser, resultId);
        var.varType = resultType;
        var.storageClass = spv::StorageClass(args[i++]);
        break;
    }

    case spv::OpTypeInt: {
        spv::Id resultId = args[i++];

        auto &type = GetType(parser, resultId);
        type.typeWidth = args[i++];
        type.typeSignedness = args[i++];
        break;
    }

    case spv::OpTypeFloat: {
        spv::Id resultId = args[i++];

        auto &type = GetType(parser, resultId);
        type.typeWidth = args[i++];
        break;
    }

    case spv::OpTypeStruct: {
        spv::Id resultId = args[i++];

        auto &type = GetType(parser, resultId);
        // TODO: members
        break;
    }

    default: {
        break;
    }
    }
}

namespace
{

auto SpvStorageClassToSpirviewResourceKind(spv::StorageClass storageClass) -> SpirviewResourceKind
{
    switch (storageClass)
    {
    case spv::StorageClass::StorageClassUniformConstant:
    case spv::StorageClass::StorageClassUniform:
        return SpirviewResourceKind::ConstantBuffer;
    case spv::StorageClass::StorageClassPushConstant:
        return SpirviewResourceKind::PushConstant;

    default:
        return SpirviewResourceKind::Unknown;
    }
}

} // namespace

}; // namespace spirview_internal

auto SpirviewReflect(std::span<const uint32_t> spirv, SpirviewReflectResult *result) -> bool
{
    using namespace spirview_internal;

    NYLA_ASSERT(spirv[0] == spv::MagicNumber);

    const uint32_t headerVersionMinor = (spirv[1] >> 8) & 0xFF;
    const uint32_t headerVersionMajor = (spirv[1] >> 16) & 0xFF;

    const uint32_t headerGeneratorMagic = spirv[2];
    const uint32_t headerIdBound = spirv[3];

    SpirvParserState state;
    for (uint32_t i = 5; i < spirv.size();)
    {
        const uint32_t word = spirv[i];

        const auto op = spv::Op(word & spv::OpCodeMask);
        const uint16_t wordCount = word >> spv::WordCountShift;
        NYLA_ASSERT(wordCount);

        ProcessOp(state, op, std::span{spirv.data() + i + 1, static_cast<uint16_t>(wordCount - 1)});
        i += wordCount;
    }

    uint32_t itype = 0;
    for (uint32_t id = 0; id < state.perId.size(); ++id)
    {
        const auto &variant = state.perId[id];

        if (std::holds_alternative<SpirvTypeIdState>(variant))
        {
            auto &type = std::get<SpirvTypeIdState>(variant);
            auto &resultType = result->records[id];

            NYLA_ASSERT(std::holds_alternative<std::monostate>(resultType));
            resultType = SpirviewType{
                .block = static_cast<bool>(type.block),
            };
            result->types[itype++] = id;

            continue;
        }
    }

    uint32_t iresource = 0;
    for (uint32_t id = 0; id < state.perId.size(); ++id)
    {
        const auto &variant = state.perId[id];

        if (std::holds_alternative<SpirvVarIdState>(variant))
        {
            auto &var = std::get<SpirvVarIdState>(variant);

            switch (var.storageClass)
            {
            case spv::StorageClass::StorageClassUniform:
            case spv::StorageClass::StorageClassUniformConstant:
            case spv::StorageClass::StorageClassStorageBuffer:
            case spv::StorageClass::StorageClassImage:
            case spv::StorageClass::StorageClassPushConstant:
                break;

            default:
                continue;
            }

            auto &resultResource = result->records[id];

            NYLA_ASSERT(std::holds_alternative<std::monostate>(resultResource));
            resultResource = SpirviewResource{
                .typeId = var.varType,
                .kind = SpvStorageClassToSpirviewResourceKind(var.storageClass),
                .set = var.set,
                .binding = var.binding,
            };
            result->resources[iresource++] = id;

            continue;
        }
    }

    result->typesCount = itype;
    result->resourcesCount = iresource;

    switch (state.executionModel)
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

    return true;
}

} // namespace nyla