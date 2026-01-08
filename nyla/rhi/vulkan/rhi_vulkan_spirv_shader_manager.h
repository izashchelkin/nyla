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
    SpirvShaderManager(std::span<uint32_t> view, RhiShaderStage stage) : m_SpvView{view}, m_Stage{stage}
    {
    }

    void Process()
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

    auto QuerySemanticLocation(std::string_view semantic, bool input, uint32_t *outLocation) -> bool
    {
        uint32_t id;
        if (!FindSemanticId(semantic, id))
            return false;

        uint32_t location;
        for (uint32_t i = 0; i < m_Locations.size(); ++i)
            if (m_Locations[i].id == id)
            {
                if (!CheckIOClass(id, input))
                    continue;

                *outLocation = m_Locations[i].location;
                return true;
            }

        return false;
    }

    auto SetSemanticLocation(std::string_view semantic, bool input, uint32_t aLocation) -> bool
    {
        uint32_t oldLocation;
        if (!QuerySemanticLocation(semantic, input, &oldLocation))
            return false;
        if (oldLocation == aLocation)
            return true;

        auto end = m_SpvView.end();
        for (auto it = m_SpvView.begin(); it != end; ++it)
        {
            auto op = spv::Op(it.Op());

            if (op == spv::OpDecorate)
            {
                SpvOperandReader operandReader = it.GetOperandReader();

                uint32_t id = operandReader.Word();
                if (!CheckIOClass(id, input))
                    continue;

                uint32_t &location = operandReader.Word();
                if (location == oldLocation)
                {
                    location = aLocation;
                    return true;
                }
            }
        }

        return false;
    }

    auto CheckIOClass(uint32_t id, bool input) -> bool
    {
        if (input)
            return IsInputStorage(id);
        else
            return IsOutputStorage(id);
    }

    auto IsInputStorage(std::string_view semantic) -> bool
    {
        uint32_t id;
        if (!FindSemanticId(semantic, id))
            return false;

        return IsInputStorage(id);
    }

    auto IsInputStorage(uint32_t id) -> bool
    {
        for (uint32_t i = 0; i < m_InputVariables.size(); ++i)
            if (id == m_InputVariables[i])
                return true;
        return false;
    }

    auto IsOutputStorage(std::string_view semantic) -> bool
    {
        uint32_t id;
        if (!FindSemanticId(semantic, id))
            return false;
        return IsInputStorage(id);
    }

    auto IsOutputStorage(uint32_t id) -> bool
    {
        for (uint32_t i = 0; i < m_OutputVariables.size(); ++i)
            if (id == m_OutputVariables[i])
                return true;
        return false;
    }

    auto FindSemanticId(std::string_view semantic, uint32_t &id) -> bool
    {
        uint32_t i;
        for (i = 0; i < m_SemanticDataNames.size(); ++i)
            if (m_SemanticDataNames[i] == semantic)
                id = m_SemanticDataIds[i];
        return i != m_SemanticDataNames.size();
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

    InlineVec<uint32_t, 16> m_SemanticDataIds;
    InlineVec<InlineString<16>, 16> m_SemanticDataNames;

    Spirview m_SpvView;
    RhiShaderStage m_Stage{};
};

} // namespace nyla