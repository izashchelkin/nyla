#pragma once

#include <cstdint>
#include <span>

#include "nyla/commons/containers/inline_string.h"
#include "nyla/commons/containers/inline_vec.h"

namespace nyla
{

enum class SpirviewShaderStage
{
    Unknown,
    Vertex,
    Fragment,
    Compute,
};

struct SpirviewReflectResult
{
    SpirviewShaderStage stage;

    InlineVec<uint32_t, 1 << 18> outSpirv;

    struct IdLocation
    {
        uint32_t id;
        uint32_t location;
    };
    InlineVec<IdLocation, 8> locations;

    struct IdSemantic
    {
        uint32_t id;
        InlineString<16> semantic;
    };
    InlineVec<IdSemantic, 8> semantics;
};

auto SpirviewReflect(std::span<const uint32_t> spirv, SpirviewReflectResult *result) -> bool;

} // namespace nyla