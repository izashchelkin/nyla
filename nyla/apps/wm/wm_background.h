#pragma once

#include <string_view>

#include "xcb/xproto.h"

namespace nyla {

extern xcb_window_t background_window;

void DrawBar(std::string_view bar_text);
void InitWMBackground();

}  // namespace nyla