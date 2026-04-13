#pragma once

#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_utils.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

INLINE auto GetShader(region_alloc &scratch, byteview name, RhiShaderStage stage) -> RhiShader
{
    auto path = RegionAlloc::AllocString<256>(scratch);
    StringWriteFmt(path, R"(D:\nyla\nyla\shaders\build\)" SV_FMT R"(.hlsl.spv)"_s, name);

    file_handle file = FileOpen(path, FileOpenMode::Read);
    span<uint8_t> data = FileReadFully(scratch, file);

    // TODO: this makes an unnecessary copy!
    RhiShader shader = Rhi::CreateShader(RhiShaderDesc{
        .stage = stage,
        .code = Span::Cast<uint32_t>(data),
    });
    return shader;
}

} // namespace nyla