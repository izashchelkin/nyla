#pragma once

#include <future>
#include <string_view>

#include "xcb/xproto.h"

namespace nyla
{

extern xcb_window_t backgroundWindow;

void DrawBackground(uint32_t numClients, std::string_view barText);
auto InitWMBackground() -> std::future<void>;

} // namespace nyla