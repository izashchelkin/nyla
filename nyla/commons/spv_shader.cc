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
#include "nyla/commons/byteparser.h"
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
    spv_reader reader{};
    ByteParser::Init(reader, operands.data, Span::SizeBytes(operands));

    auto executionModel = (spv_execution_model)ByteParser::Read32(reader);
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
    spv_reader reader{};
    ByteParser::Init(reader, operands.data, Span::SizeBytes(operands));

    byteview name = SpvReader::ReadString(reader);
    if (Span::Eq(name, "SPV_GOOGLE_hlsl_functionality1"_s))
        return spv_op_process_result::Remove;
    if (Span::Eq(name, "SPV_GOOGLE_user_type"_s))
        return spv_op_process_result::Remove;

    return spv_op_process_result::Ok;
}

auto HandleOpDecorate(spv_shader &self, span<uint32_t> operands) -> spv_op_process_result
{
    spv_reader reader{};
    ByteParser::Init(reader, operands.data, Span::SizeBytes(operands));

    uint32_t targetId = ByteParser::Read32(reader);

    switch ((spv_decoration)ByteParser::Read32(reader))
    {
    case spv_decoration::Location: {
        const uint32_t location = ByteParser::Read32(reader);
        InlineVec::Append(self.locations, spv_shader::location_data{
                                              .id = targetId,
                                              .location = location,
                                          });
        return spv_op_process_result::Ok;
    }

    case spv_decoration::BuiltIn: {
        const uint32_t builtin = ByteParser::Read32(reader);
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
    spv_reader reader{};
    ByteParser::Init(reader, operands.data, Span::SizeBytes(operands));

    uint32_t targetId = ByteParser::Read32(reader);

    switch ((spv_decoration)ByteParser::Read32(reader))
    {
    case spv_decoration::UserSemantic: {
        InlineVec::Append(self.semanticDataIds, targetId);

        inline_string<16> &name = InlineVec::Append(self.semanticDataNames);
        byteview src = SpvReader::ReadString(reader);
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
    spv_reader reader{};
    ByteParser::Init(reader, operands.data, Span::SizeBytes(operands));

    uint32_t resultType = ByteParser::Read32(reader);
    uint32_t resultId = ByteParser::Read32(reader);

    switch ((spv_storage_class)ByteParser::Read32(reader))
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

auto API ProcessShader(spv_shader &self, span<uint32_t> data) -> span<uint32_t>
{
    spv_reader reader{};
    ByteParser::Init(reader, data.data, Span::SizeBytes(data));

    SpvReader::ReadHeader(reader);

    while (ByteParser::BytesLeft(reader))
    {
        uint32_t word = *(uint32_t *)reader.at;
        spv_op op = (spv_op)(word & 0xFFFF);
        uint16_t wordCount = word >> 16;
        span<uint32_t> operands{(uint32_t *)reader.at + 1, (uint64_t)wordCount - 1};

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
            ByteParser::Advance(reader, uint64_t{wordCount} * 4);
            break;
        }
        case spv_op_process_result::InvalidState: {
            ASSERT(false);
            break;
        }
        case spv_op_process_result::Remove: {
            data = Span::Erase(data, (uint32_t *)reader.at, (uint32_t *)reader.at + wordCount);
            reader.end -= uint64_t{wordCount} * 4;
            break;
        }
        }
    }

    return data;
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

    spv_reader reader{};
    ByteParser::Init(reader, data.data, Span::SizeBytes(data));

    SpvReader::ReadHeader(reader);

    while (data.size)
    {
        uint32_t word = *(uint32_t *)reader.at;
        spv_op op = (spv_op)(word & 0xFFFF);
        uint16_t wordCount = word >> 16;
        span<uint32_t> operands{(uint32_t *)reader.at + 1, (uint64_t)wordCount - 1};

        if ((spv_op)op != spv_op::Decorate)
            continue;

        uint32_t id = ByteParser::Read32(reader);

        if ((spv_decoration)ByteParser::Read32(reader) != spv_decoration::Location)
            continue;
        if (!CheckStorageClass(self, id, storageClass))
            continue;

        uint32_t &location = *(uint32_t *)reader.at;
        if (location == oldLocation)
        {
            location = aLocation;
            return true;
        }

        ByteParser::Read32(reader);
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