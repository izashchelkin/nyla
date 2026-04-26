#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/bdf.h"
#include "nyla/commons/color.h"
#include "nyla/commons/debug_text_renderer.h"
#include "nyla/commons/engine.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/file.h"
#include "nyla/commons/gamepad.h"
#include "nyla/commons/glyph_renderer.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/input_manager.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/lerp.h"
#include "nyla/commons/limits.h"
#include "nyla/commons/macros.h" // IWYU pragma: keep
#include "nyla/commons/mat.h"
#include "nyla/commons/math.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/mesh_manager.h"
#include "nyla/commons/minmax.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/render_targets.h"
#include "nyla/commons/renderer.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/sampler_manager.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/spv_shader_enums.h"
#include "nyla/commons/texture_manager.h"
#include "nyla/commons/time.h"
#include "nyla/commons/tween_manager.h"
#include "nyla/commons/vec.h"
#include <cstdint>

namespace nyla
{

void UserMain()
{
    auto alloc = RegionAlloc::Create(MemPagePool::kChunkSize, 0);

    auto &searchList = RegionAlloc::AllocVec<byteview, 256>(alloc);
    InlineVec::Append(searchList, R"(assets)"_s);

    while (searchList.size > 0)
    {
        byteview currentDir = InlineVec::PopBack(searchList);

        file_metadata meta;
        dir_iter *dirIter = DirIter::Create(alloc, currentDir);
        if (dirIter)
        {
            while (DirIter::Next(alloc, *dirIter, meta))
            {
                if (Any(meta.attributes & file_attribute::Hidden))
                    continue;
                if (Span::Eq(meta.fileName, "."_s))
                    continue;
                if (Span::Eq(meta.fileName, ".."_s))
                    continue;

                if (Any(meta.attributes & file_attribute::Directory))
                {
                    auto &subDir = RegionAlloc::AllocVec<uint8_t, 0x100>(alloc);
                    InlineVec::Append(subDir, currentDir);
                    InlineVec::Append(subDir, "/"_s);
                    InlineVec::Append(subDir, meta.fileName);

                    InlineVec::Append(searchList, subDir);
                }
                else
                {
                    auto& fullPath = RegionAlloc::AllocVec<uint8_t, 0x100>(alloc);
                    InlineVec::Append(fullPath, currentDir);
                    InlineVec::Append(fullPath, "/"_s);
                    InlineVec::Append(fullPath, meta.fileName);

                    LOG("file: " SV_FMT, SV_ARG(fullPath));
                }
            };

            DirIter::Destroy(*dirIter);
        }
    }

    GenRandom64();
}

} // namespace nyla