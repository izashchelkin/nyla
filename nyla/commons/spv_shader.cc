#include "nyla/commons/spv_shader.h"
#include "nyla/commons/spv_reader.h"

namespace nyla
{

namespace SpvShader
{

void Init(spv_shader &self, span<uint32_t> data, RhiShaderStage expectedStage)
{
    SpvReader::ReadHeader(data);
    while (data.size)
    {
        span<uint32_t> opWithOperands = SpvReader::ReadOpWithOperands(data);
        switch (SpirvReader::ParseOp(Span::Front(opWithOperands)))
        {

#define X(name)                                                                                                        \
    case spv::name: {                                                                                                  \
        name(it);                                                                                                      \
        break;                                                                                                         \
    }
            X(OpEntryPoint)
            X(OpExtension)
            X(OpDecorate)
            X(OpDecorateString)
            X(OpVariable)
        }
    }
}

#if 0
auto FindLocationBySemantic(Str semantic, StorageClass storageClass, uint32_t *outLocation) -> bool
{
    uint32_t id;
    if (!FindIdBySemantic(semantic, storageClass, &id))
        return false;

    uint32_t location;
    for (uint32_t i = 0; i < m_Locations.Size(); ++i)
    {
        if (m_Locations[i].id == id && CheckStorageClass(id, storageClass))
        {
            *outLocation = m_Locations[i].location;
            return true;
        }
    }

    return false;
}

auto FindIdBySemantic(Str querySemantic, StorageClass storageClass, uint32_t *outId) -> bool
{
    for (uint32_t i = 0; i < m_SemanticDataNames.Size(); ++i)
    {
        const auto &semantic = m_SemanticDataNames[i];
        if (semantic == querySemantic && CheckStorageClass(m_SemanticDataIds[i], storageClass))
        {
            *outId = m_SemanticDataIds[i];
            return true;
        }
    }
    return false;
}

auto FindSemanticById(uint32_t id, Str *outSemantic) -> bool
{
    for (uint32_t i = 0; i < m_SemanticDataIds.Size(); ++i)
    {
        if (m_SemanticDataIds[i] == id)
        {
            *outSemantic = m_SemanticDataNames[i].GetStr();
            return true;
        }
    }
    return false;
}

auto RewriteLocationForSemantic(Str semantic, StorageClass storageClass, uint32_t aLocation) -> bool
{
    uint32_t oldLocation;
    if (!FindLocationBySemantic(semantic, storageClass, &oldLocation))
        return false;

    if (oldLocation == aLocation)
        return true;

    auto end = m_SpvView.end();
    for (auto it = m_SpvView.begin(); it != end; ++it)
    {
        auto op = spv::Op(it.Op());

        if (op != spv::OpDecorate)
            continue;

        SpvOperandReader operandReader = it.GetOperandReader();

        uint32_t id = operandReader.Word();

        if (spv::Decoration(operandReader.Word()) != spv::DecorationLocation)
            continue;
        if (!CheckStorageClass(id, storageClass))
            continue;

        uint32_t &location = operandReader.Word();
        if (location == oldLocation)
        {
            location = aLocation;
            return true;
        }
    }

    return false;
}
#endif

namespace
{

void OpEntryPoint(span<uint32_t> data)
{
    SpvOperandReader operandReader = it.GetOperandReader();

    auto executionModel = spv::ExecutionModel(operandReader.Word());
    switch (executionModel)
    {
    case spv::ExecutionModel::ExecutionModelVertex:
        NYLA_ASSERT(m_Stage == RhiShaderStage::Vertex);
        break;
    case spv::ExecutionModel::ExecutionModelFragment:
        NYLA_ASSERT(m_Stage == RhiShaderStage::Pixel);
        break;
    default:
        NYLA_ASSERT(false);
        break;
    }
}

void OpExtension(Spirview::BasicIterator<uint32_t> it)
{
    SpvOperandReader operandReader = it.GetOperandReader();

    Str name = operandReader.String();
    if (name == AsStr("SPV_GOOGLE_hlsl_functionality1"))
        goto nop;
    if (name == AsStr("SPV_GOOGLE_user_type"))
        goto nop;

    return;
nop:
    it.MakeNop();
}

void OpDecorate(Spirview::BasicIterator<uint32_t> it)
{
    SpvOperandReader operandReader = it.GetOperandReader();

    uint32_t targetId = operandReader.Word();

    switch (spv::Decoration(operandReader.Word()))
    {
    case spv::Decoration::DecorationLocation: {
        const uint32_t location = operandReader.Word();
        m_Locations.PushBack(LocationData{
            .id = targetId,
            .location = location,
        });
        break;
    }

    case spv::Decoration::DecorationBuiltIn: {
        const uint32_t builtin = operandReader.Word();
        m_Builtin.PushBack(BuiltinData{
            .id = targetId,
            .builtin = builtin,
        });
        break;
    }
    }
}

void OpDecorateString(Spirview::BasicIterator<uint32_t> it)
{
    SpvOperandReader operandReader = it.GetOperandReader();

    const uint32_t targetId = operandReader.Word();

    switch (spv::Decoration(operandReader.Word()))
    {
    case spv::Decoration::DecorationUserSemantic: {
        m_SemanticDataIds.PushBack(targetId);

        auto &name = m_SemanticDataNames.PushBack();
        auto src = operandReader.String();
        MemCpy(name.Data(), src.Data(), src.Size());

        name.AsciiToUpper();

        NYLA_LOG("" NYLA_SV_FMT, NYLA_SV_ARG(name.GetStr()));
        break;
    }
    }

    it.MakeNop();
}

void OpVariable(Spirview::BasicIterator<uint32_t> it)
{
    SpvOperandReader operandReader = it.GetOperandReader();

    const uint32_t resultType = operandReader.Word();
    const uint32_t resultId = operandReader.Word();

    switch (static_cast<spv::StorageClass>(operandReader.Word()))
    {
    case spv::StorageClass::StorageClassInput:
        m_InputVariables.PushBack(resultId);
        break;
    case spv::StorageClass::StorageClassOutput:
        m_OutputVariables.PushBack(resultId);
        break;
    default:
        break;
    }
}

} // namespace

} // namespace SpvShader

} // namespace nyla