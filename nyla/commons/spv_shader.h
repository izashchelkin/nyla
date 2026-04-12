#pragma once

#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

struct spv_shader
{
    RhiShaderStage stage;

    inline_vec<uint32_t, 8> inputVariables;
    inline_vec<uint32_t, 8> outputVariables;

    struct location_data
    {
        uint32_t id;
        uint32_t location;
    };
    inline_vec<location_data, 16> locations;

    struct builtin_data
    {
        uint32_t id;
        uint32_t builtin;
    };
    inline_vec<builtin_data, 16> builtins;

    inline_vec<uint32_t, 16> semanticDataIds;
    inline_vec<inline_string<16>, 16> semanticDataNames;
};

enum class spv_shader_storage_class
{
    Input,
    Output,
};

namespace SpvShader
{

void ProcessShader(spv_shader &self, span<uint32_t> data, RhiShaderStage stage);

auto FindLocationBySemantic(spv_shader &self, byteview semantic, spv_shader_storage_class storageClass,
                            uint32_t *outLocation) -> bool;

auto FindIdBySemantic(spv_shader &self, byteview querySemantic, spv_shader_storage_class storageClass, uint32_t *outId)
    -> bool;

auto FindSemanticById(spv_shader &self, uint32_t id, byteview *outSemantic) -> bool;

auto RewriteLocationForSemantic(spv_shader &self, byteview semantic, spv_shader_storage_class storageClass,
                                uint32_t aLocation) -> bool;

auto CheckStorageClass(spv_shader &self, uint32_t id, spv_shader_storage_class storageClass) -> bool;

INLINE auto CheckStorageClass(spv_shader &self, byteview semantic, spv_shader_storage_class storageClass) -> bool
{
    uint32_t id;
    if (FindIdBySemantic(self, semantic, storageClass, &id))
        return CheckStorageClass(self, id, storageClass);

    return false;
}

#if 0
auto GetInputIds() -> span<const uint32_t>
{
    return m_InputVariables.GetSpan().AsConst();
}

auto GetSemantics() -> span<InlineString<16>>
{
    return m_SemanticDataNames.GetSpan();
}
#endif

} // namespace SpvShader

} // namespace nyla