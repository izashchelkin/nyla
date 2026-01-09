#pragma once

#include "nyla/commons/assert.h"
#include "nyla/commons/containers/inline_string.h"
#include "nyla/commons/containers/inline_vec.h"
#include "nyla/rhi/rhi_shader.h"
#include "nyla/spirview/spirview.h"
#include <cstdint>
#include <span>
#include <string_view>

#define spv_enable_utility_code
#if defined(__linux__)
#include <spirv/unified1/spirv.hpp>
#else
#include <spirv-headers/spirv.hpp>
#endif

namespace nyla
{

class SpirvShaderManager
{
  public:
    enum class StorageClass
    {
        Input,
        Output,
    };

    SpirvShaderManager(std::span<uint32_t> view, RhiShaderStage stage) : m_SpvView{view}, m_Stage{stage}
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

    auto FindLocationBySemantic(std::string_view semantic, StorageClass storageClass, uint32_t *outLocation) -> bool
    {
        uint32_t id;
        if (!FindIdBySemantic(semantic, storageClass, &id))
            return false;

        uint32_t location;
        for (uint32_t i = 0; i < m_Locations.size(); ++i)
        {
            if (m_Locations[i].id == id && CheckStorageClass(id, storageClass))
            {
                *outLocation = m_Locations[i].location;
                return true;
            }
        }

        return false;
    }

    auto FindIdBySemantic(std::string_view semantic, StorageClass storageClass, uint32_t *outId) -> bool
    {
        for (uint32_t i = 0; i < m_SemanticDataNames.size(); ++i)
        {
            if (m_SemanticDataNames[i] == semantic && CheckStorageClass(m_SemanticDataIds[i], storageClass))
            {
                *outId = m_SemanticDataIds[i];
                return true;
            }
        }
        return false;
    }

    auto FindSemanticById(uint32_t id, std::string_view *outSemantic) -> bool
    {
        for (uint32_t i = 0; i < m_SemanticDataIds.size(); ++i)
        {
            if (m_SemanticDataIds[i] == id)
            {
                *outSemantic = m_SemanticDataNames[i].StringView();
                return true;
            }
        }
        return false;
    }

    auto RewriteLocationForSemantic(std::string_view semantic, StorageClass storageClass, uint32_t aLocation) -> bool
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
            for (uint32_t i = 0; i < m_InputVariables.size(); ++i)
                if (id == m_InputVariables[i])
                    return true;
            return false;
        }
        case StorageClass::Output: {
            for (uint32_t i = 0; i < m_OutputVariables.size(); ++i)
                if (id == m_OutputVariables[i])
                    return true;
            return false;
        }
        default: {
            NYLA_ASSERT(false);
        }
        }
    }

    auto CheckStorageClass(std::string_view semantic, StorageClass storageClass) -> bool
    {
        if (uint32_t id; FindIdBySemantic(semantic, storageClass, &id))
            return CheckStorageClass(id, storageClass);
        return false;
    }

    auto GetInputIds() -> std::span<const uint32_t>
    {
        return m_InputVariables;
    }

    auto GetSemantics() -> std::span<InlineString<16>>
    {
        return m_SemanticDataNames;
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

        std::string_view name = operandReader.String();
        if (name == "SPV_GOOGLE_hlsl_functionality1")
            goto nop;
        if (name == "SPV_GOOGLE_user_type")
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
            m_Locations.emplace_back(LocationData{
                .id = targetId,
                .location = location,
            });
            break;
        }

        case spv::Decoration::DecorationBuiltIn: {
            const uint32_t builtin = operandReader.Word();
            m_Builtin.emplace_back(BuiltinData{
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
            m_SemanticDataIds.emplace_back(targetId);

            auto &name = m_SemanticDataNames.emplace_back(operandReader.String());
            name.AsciiToUpper();
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
            m_InputVariables.emplace_back(resultId);
            break;
        case spv::StorageClass::StorageClassOutput:
            m_OutputVariables.emplace_back(resultId);
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