#pragma once

#include "nyla/vulkan/render_pipeline.h"

namespace nyla {

extern Rp gui_pipeline;

void UI_BoxBegin();
void UI_Text(std::string_view text);

}  // namespace nyla