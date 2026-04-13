/*
** Copyright: 2014-2024 The Khronos Group Inc.
** License: MIT
**
** MODIFICATIONS TO THIS FILE MAY MEAN IT NO LONGER ACCURATELY REFLECTS
** KHRONOS STANDARDS. THE UNMODIFIED, NORMATIVE VERSIONS OF KHRONOS
** SPECIFICATIONS AND HEADER INFORMATION ARE LOCATED AT
** https://www.khronos.org/registry/
*/

#pragma once

#include "nyla/commons/inline_string.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

struct spv_shader
{
    rhi_shader_stage stage;

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

INLINE auto GetInputIds(spv_shader &self) -> span<const uint32_t>
{
    return self.inputVariables;
}

INLINE auto GetSemantics(spv_shader &self) -> span<const inline_string<16>>
{
    return self.semanticDataNames;
}

void ProcessShader(spv_shader &self, span<uint32_t> data, rhi_shader_stage stage);

auto FindLocationBySemantic(spv_shader &self, byteview semantic, spv_shader_storage_class storageClass,
                            uint32_t *outLocation) -> bool;

auto FindIdBySemantic(spv_shader &self, byteview querySemantic, spv_shader_storage_class storageClass, uint32_t *outId)
    -> bool;

auto FindSemanticById(spv_shader &self, uint32_t id, byteview *outSemantic) -> bool;

auto RewriteLocationForSemantic(spv_shader &self, span<uint32_t> data, byteview semantic,
                                spv_shader_storage_class storageClass, uint32_t aLocation) -> bool;

auto CheckStorageClass(spv_shader &self, uint32_t id, spv_shader_storage_class storageClass) -> bool;

INLINE auto CheckStorageClass(spv_shader &self, byteview semantic, spv_shader_storage_class storageClass) -> bool
{
    uint32_t id;
    if (FindIdBySemantic(self, semantic, storageClass, &id))
        return CheckStorageClass(self, id, storageClass);

    return false;
}

} // namespace SpvShader

} // namespace nyla