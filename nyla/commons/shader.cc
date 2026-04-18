#include "nyla/commons/shader.h"

#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

auto API GetShader(region_alloc &alloc, byteview name, rhi_shader_stage stage) -> rhi_shader
{
    void *allocMark = alloc.at;

    auto path = RegionAlloc::AllocString<256>(alloc);
    StringWriteFmt(path, R"(D:\nyla\nyla\shaders\build\)" SV_FMT R"(.hlsl.spv)"_s, name);

    file_handle file = FileOpen(path, FileOpenMode::Read);
    span<uint8_t> data = FileReadFully(alloc, file);

    // TODO: this makes an unnecessary copy!
    rhi_shader shader = Rhi::CreateShader(rhi_shader_desc{
        .stage = stage,
        .code = Span::Cast<uint32_t>(data),
    });

    RegionAlloc::Reset(alloc, allocMark);
    return shader;
}

} // namespace nyla