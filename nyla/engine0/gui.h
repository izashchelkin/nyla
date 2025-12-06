#pragma once

#include <cstdint>

#include "nyla/engine0/render_pipeline.h"

namespace nyla
{

extern Rp guiPipeline;

void UiFrameBegin();
void UiBoxBegin(int32_t x, int32_t y, uint32_t width, uint32_t height);
void UiText(std::string_view text);

} // namespace nyla