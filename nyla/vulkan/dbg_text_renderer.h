#pragma once

#include "nyla/vulkan/render_pipeline.h"

namespace nyla {

extern Rp dbg_text_pipeline;

void DbgText(int32_t x, int32_t y, std::string_view text);

}  // namespace nyla