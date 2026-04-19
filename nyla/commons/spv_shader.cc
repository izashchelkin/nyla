/*x
** Copyright: 2014-2024 The Khronos Group Inc.
** License: MIT
**
** MODIFICATIONS TO THIS FILE MAY MEAN IT NO LONGER ACCURATELY REFLECTS
** KHRONOS STANDARDS. THE UNMODIFIED, NORMATIVE VERSIONS OF KHRONOS
** SPECIFICATIONS AND HEADER INFORMATION ARE LOCATED AT
** https://www.khronos.org/registry/
*/

#include "nyla/commons/spv_shader.h"

#include <cstdint>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span.h"
#include "nyla/commons/spv_reader.h"
#include "nyla/commons/spv_shader_enums.h"

namespace nyla
{

namespace SpvShader
{

namespace
{

enum class spv_op_process_result
{
    Ok,
    InvalidState,
    Remove,
};

auto HandleOpNop(spv_shader &self, span<uint32_t> operands) -> spv_op_process_result
{
    TRAP();
    return spv_op_process_result::Ok;
}

auto HandleOpEntryPoint(spv_shader &self, span<uint32_t> operands) -> spv_op_process_result
{
    auto executionModel = (spv_execution_model)SpvReader::ReadWord(operands);
    switch (executionModel)
    {
    case spv_execution_model::Vertex: {
        if (self.stage == rhi_shader_stage::Vertex)
            return spv_op_process_result::Ok;

        return spv_op_process_result::InvalidState;
    }

    case spv_execution_model::Fragment: {
        if (self.stage == rhi_shader_stage::Pixel)
            return spv_op_process_result::Ok;

        return spv_op_process_result::InvalidState;
    }

    default:
        return spv_op_process_result::InvalidState;
    }
}

auto HandleOpExtension(spv_shader &self, span<uint32_t> operands) -> spv_op_process_result
{
    byteview name = SpvReader::ReadString(operands);
    if (Span::Eq(name, "SPV_GOOGLE_hlsl_functionality1"_s))
        return spv_op_process_result::Remove;
    if (Span::Eq(name, "SPV_GOOGLE_user_type"_s))
        return spv_op_process_result::Remove;

    return spv_op_process_result::Ok;
}

auto HandleOpDecorate(spv_shader &self, span<uint32_t> operands) -> spv_op_process_result
{
    uint32_t targetId = SpvReader::ReadWord(operands);

    switch ((spv_decoration)SpvReader::ReadWord(operands))
    {
    case spv_decoration::Location: {
        const uint32_t location = SpvReader::ReadWord(operands);
        InlineVec::Append(self.locations, spv_shader::location_data{
                                              .id = targetId,
                                              .location = location,
                                          });
        return spv_op_process_result::Ok;
    }

    case spv_decoration::BuiltIn: {
        const uint32_t builtin = SpvReader::ReadWord(operands);
        InlineVec::Append(self.builtins, spv_shader::builtin_data{
                                             .id = targetId,
                                             .builtin = builtin,
                                         });
        return spv_op_process_result::Ok;
    }

    default: {
        return spv_op_process_result::Ok;
    }
    }
}

auto HandleOpDecorateString(spv_shader &self, span<uint32_t> operands) -> spv_op_process_result
{
    uint32_t targetId = SpvReader::ReadWord(operands);

    switch ((spv_decoration)SpvReader::ReadWord(operands))
    {
    case spv_decoration::UserSemantic: {
        InlineVec::Append(self.semanticDataIds, targetId);

        inline_string<16> &name = InlineVec::Append(self.semanticDataNames);
        byteview src = SpvReader::ReadString(operands);
        InlineVec::Resize(name, src.size);
        MemCpy(name.data.data, src.data, src.size);

        InlineString::AsciiToUpper(name);

        LOG("" SV_FMT, SV_ARG((byteview)name));
        return spv_op_process_result::Ok;
    }

    default: {
        return spv_op_process_result::Remove;
    }
    }
}

auto HandleOpVariable(spv_shader &self, span<uint32_t> operands) -> spv_op_process_result
{
    uint32_t resultType = SpvReader::ReadWord(operands);
    uint32_t resultId = SpvReader::ReadWord(operands);

    switch ((spv_storage_class)SpvReader::ReadWord(operands))
    {
    case spv_storage_class::Input: {
        InlineVec::Append(self.inputVariables, resultId);
        return spv_op_process_result::Ok;
    }
    case spv_storage_class::Output: {
        InlineVec::Append(self.outputVariables, resultId);
        return spv_op_process_result::Ok;
    }
    default:
        return spv_op_process_result::Ok;
    }
}

} // namespace

auto API ProcessShader(spv_shader &self, span<uint32_t> data, rhi_shader_stage stage) -> span<uint32_t>
{
    span<uint32_t> ret = data;

    SpvReader::ReadHeader(data);
    while (data.size)
    {
        span<uint32_t> opWithOperands = SpvReader::ReadOpWithOperands(data);
        uint32_t op = SpvReader::ParseOp(Span::Front(opWithOperands));
        span<uint32_t> operands = Span::SubSpan(opWithOperands, 1);

        spv_op_process_result result = spv_op_process_result::Ok;
        switch ((spv_op)op)
        {

#define X(name)                                                                                                        \
    case spv_op::name: {                                                                                               \
        result = HandleOp##name(self, operands);                                                                       \
        break;                                                                                                         \
    }
            X(EntryPoint)
            X(Extension)
            X(Decorate)
            X(DecorateString)
            X(Variable)
            X(Nop)

#undef X

        default: {
            break;
        }
        }

        switch (result)
        {
        case spv_op_process_result::Ok: {
            break;
        }
        case spv_op_process_result::InvalidState: {
            ASSERT(false);
            break;
        }
        case spv_op_process_result::Remove: {
            ret = Span::Erase(ret, opWithOperands);
            data = Span::Erase(data, opWithOperands);
            break;
        }
        }
    }

    return ret;
}

auto API FindIdBySemantic(spv_shader &self, byteview querySemantic, spv_shader_storage_class storageClass,
                          uint32_t *outId) -> bool
{
    for (uint32_t i = 0; i < self.semanticDataNames.size; ++i)
    {
        const auto &semantic = self.semanticDataNames[i];
        if (Span::Eq((byteview)semantic, querySemantic) &&
            CheckStorageClass(self, self.semanticDataIds[i], storageClass))
        {
            *outId = self.semanticDataIds[i];
            return true;
        }
    }
    return false;
}

auto API FindLocationBySemantic(spv_shader &self, byteview semantic, spv_shader_storage_class storageClass,
                                uint32_t *outLocation) -> bool
{
    uint32_t id;
    if (!FindIdBySemantic(self, semantic, storageClass, &id))
        return false;

    uint32_t location;
    for (uint32_t i = 0; i < self.locations.size; ++i)
    {
        if (self.locations[i].id == id && CheckStorageClass(self, id, storageClass))
        {
            *outLocation = self.locations[i].location;
            return true;
        }
    }

    return false;
}

auto API FindSemanticById(spv_shader &self, uint32_t id, byteview *outSemantic) -> bool
{
    for (uint32_t i = 0; i < self.semanticDataIds.size; ++i)
    {
        if (self.semanticDataIds[i] == id)
        {
            *outSemantic = (byteview)self.semanticDataNames[i];
            return true;
        }
    }
    return false;
}

auto API RewriteLocationForSemantic(spv_shader &self, span<uint32_t> data, byteview semantic,
                                    spv_shader_storage_class storageClass, uint32_t aLocation) -> bool
{
    uint32_t oldLocation;
    if (!FindLocationBySemantic(self, semantic, storageClass, &oldLocation))
        return false;

    if (oldLocation == aLocation)
        return true;

    SpvReader::ReadHeader(data);

    while (data.size)
    {
        span<uint32_t> opWithOperands = SpvReader::ReadOpWithOperands(data);
        uint32_t op = SpvReader::ParseOp(Span::Front(opWithOperands));

        if ((spv_op)op != spv_op::Decorate)
            continue;

        span<uint32_t> operands = Span::SubSpan(opWithOperands, 1);

        uint32_t id = SpvReader::ReadWord(data);

        if ((spv_decoration)SpvReader::ReadWord(data) != spv_decoration::Location)
            continue;
        if (!CheckStorageClass(self, id, storageClass))
            continue;

        uint32_t &location = SpvReader::ReadWord(data);
        if (location == oldLocation)
        {
            location = aLocation;
            return true;
        }
    }

    return false;
}

auto API CheckStorageClass(spv_shader &self, uint32_t id, spv_shader_storage_class storageClass) -> bool
{
    switch (storageClass)
    {
    case spv_shader_storage_class::Output: {
        return Span::Contains((span<uint32_t>)self.outputVariables, id);
    }
    case spv_shader_storage_class::Input: {
        return Span::Contains((span<uint32_t>)self.inputVariables, id);
    }
    }
    TRAP();
    UNREACHABLE();
}

} // namespace SpvShader

} // namespace nyla