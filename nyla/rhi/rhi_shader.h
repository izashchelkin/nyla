#pragma once

#include "nyla/commons/bitenum.h"
#include "nyla/rhi/rhi_handle.h"

namespace nyla
{

enum class RhiShaderStage : uint32_t
{
    Vertex = 1 << 0,
    Fragment = 1 << 1,
};

template <> struct EnableBitMaskOps<RhiShaderStage> : std::true_type
{
};

struct RhiShader : RhiHandle
{
};

struct RhiShaderDesc
{
    std::vector<std::byte> code;
};

auto RhiCreateShader(const RhiShaderDesc &) -> RhiShader;
void RhiDestroyShader(RhiShader);

} // namespace nyla