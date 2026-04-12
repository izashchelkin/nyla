#pragma once

#include <cstdint>

#include "nyla/commons/fmt.h"
#include "nyla/commons/math.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/platform.h"

namespace nyla
{

struct spv_shader_header
{
    uint32_t version;
    uint32_t generator;
    uint32_t bound;
};

namespace SpvReader
{

[[nodiscard]]
auto ReadWord(span<uint32_t> &data) -> uint32_t
{
    uint32_t &ret = Span::Front(data);
    data = Span::SubSpan(data, 1);
    return ret;
}

[[nodiscard]]
auto ReadString(span<uint32_t> &data) -> byteview
{
    uint8_t *ptr = (uint8_t *)data.data;
    uint64_t byteLen = CStrLen(ptr, data.size) + 1;

    uint64_t wordLen = CeilDiv(byteLen, 4);
    data = Span::SubSpan(data, wordLen);

    return byteview{ptr, byteLen};
}

constexpr inline uint32_t kMagicNumber = 0x07230203;
constexpr inline uint32_t kOpCodeMask = 0xFFFF;
constexpr inline uint32_t kWordCountShift = 16;
constexpr inline uint32_t kNop = 1 << kWordCountShift;

[[nodiscard]]
INLINE auto VersionMajor(uint32_t version) -> uint8_t
{
    return (version >> 16) & 0xFF;
}

[[nodiscard]]
INLINE auto VersionMinor(uint32_t version) -> uint8_t
{
    return (version >> 8) & 0xFF;
}

[[nodiscard]]
INLINE auto ParseOp(uint32_t word) -> uint16_t
{
    return word & kOpCodeMask;
}

[[nodiscard]]
INLINE auto ParseWordCount(uint32_t word) -> uint16_t
{
    return word >> kWordCountShift;
}

[[nodiscard]]
INLINE auto ReadHeader(span<uint32_t> &data) -> spv_shader_header
{
    NYLA_ASSERT(data.size >= 5);
    NYLA_ASSERT(data[0] == kMagicNumber);

    uint32_t version = ReadWord(data);
    uint32_t generator = ReadWord(data);
    uint32_t bound = ReadWord(data);
    uint32_t reserved = ReadWord(data);
    NYLA_ASSERT(reserved == 0);

    return spv_shader_header{
        .version = version,
        .generator = generator,
        .bound = bound,
    };
}

[[nodiscard]]
INLINE auto ReadOpWithOperands(span<uint32_t> &data) -> span<uint32_t>
{
    uint32_t wordCount = ParseWordCount(Span::Front(data));
    data = Span::SubSpan(data, wordCount);
    return span{data.data, wordCount};
}

INLINE void MakeNop(span<uint32_t> &data)
{
    MemSet(data.data, ParseWordCount(Span::Front(data)), kNop);

} // namespace SpvReader

class SpirvShaderManager
{
  public:
    enum class StorageClass
    {
        Input,
        Output,
    };

    SpirvShaderManager(Span<uint32_t> view, RhiShaderStage stage) : m_SpvView{view}, m_Stage{stage}
    {
        auto end = m_SpvView.end();
        for (auto it = m_SpvView.begin(); it != end; ++it)
        {
            auto op = spv::Op(it.Op());

            switch (op)
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

    auto CheckStorageClass(uint32_t id, StorageClass storageClass) -> bool
    {
        switch (storageClass)
        {
        case StorageClass::Input: {
            for (uint32_t i = 0; i < m_InputVariables.Size(); ++i)
                if (id == m_InputVariables[i])
                    return true;
            return false;
        }
        case StorageClass::Output: {
            for (uint32_t i = 0; i < m_OutputVariables.Size(); ++i)
                if (id == m_OutputVariables[i])
                    return true;
            return false;
        }
        default: {
            NYLA_ASSERT(false);
        }
        }
    }

    auto CheckStorageClass(Str semantic, StorageClass storageClass) -> bool
    {
        if (uint32_t id; FindIdBySemantic(semantic, storageClass, &id))
            return CheckStorageClass(id, storageClass);
        return false;
    }

    auto GetInputIds() -> Span<const uint32_t>
    {
        return m_InputVariables.GetSpan().AsConst();
    }

    auto GetSemantics() -> Span<InlineString<16>>
    {
        return m_SemanticDataNames.GetSpan();
    }

  private:
    void OpEntryPoint(Spirview::BasicIterator<uint32_t> it)
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

    InlineVec<uint32_t, 8> m_InputVariables;
    InlineVec<uint32_t, 8> m_OutputVariables;

    struct LocationData
    {
        uint32_t id;
        uint32_t location;
    };
    InlineVec<LocationData, 16> m_Locations;

    struct BuiltinData
    {
        uint32_t id;
        uint32_t builtin;
    };
    InlineVec<BuiltinData, 16> m_Builtin;

    InlineVec<uint32_t, 16> m_SemanticDataIds;
    InlineVec<InlineString<16>, 16> m_SemanticDataNames;

    Spirview m_SpvView;
    RhiShaderStage m_Stage{};
};

} // namespace nyla