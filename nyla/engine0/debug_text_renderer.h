#pragma once

#include "nyla/engine0/render_pipeline.h"

namespace nyla
{

extern Rp dbgTextPipeline;

void DbgText(int32_t x, int32_t y, std::string_view text);

} // namespace nyla