#pragma once

#include <cstdint>

#include "nyla/vulkan/render_pipeline.h"

namespace nyla {

extern Rp gui_pipeline;

void UI_FrameBegin();
void UI_BoxBegin(int32_t x, int32_t y, uint32_t width, uint32_t height);
void UI_Text(std::string_view text);

}  // namespace nyla