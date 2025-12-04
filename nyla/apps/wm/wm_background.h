#pragma once

#include <future>
#include <string_view>

#include "xcb/xproto.h"

namespace nyla
{

extern xcb_window_t background_window;

void DrawBackground(uint32_t num_clients, std::string_view bar_text);
std::future<void> InitWMBackground();

} // namespace nyla