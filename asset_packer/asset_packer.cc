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
#include "nyla/commons/input_manager.h"
#include "nyla/commons/keyboard.h"
#include "nyla/commons/lerp.h"
#include "nyla/commons/limits.h"
#include "nyla/commons/macros.h" // IWYU pragma: keep
#include "nyla/commons/mat.h"
#include "nyla/commons/math.h"
#include "nyla/commons/mem.h"
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

namespace nyla
{

void UserMain()
{
    file_handle walker = FileWalkBegin("R(assets)"_s);
    for (file_metadata metadata; FileWalkNext(walker, metadata);)
    {
        LOG("file: " SV_FMT, SV_ARG(metadata.name));
    }
    FileWalkEnd(walker);

    GenRandom64();
}

} // namespace nyla