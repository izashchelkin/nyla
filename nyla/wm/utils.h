#ifndef NYLA_WM_UTILS_H
#define NYLA_WM_UTILS_H

#include <xcb/xproto.h>

#include <cstdint>
#include <string_view>

#include "nyla/layout/rect.h"
#include "xcb/xcb.h"

namespace nyla {

Rect GetBoundingRect(xcb_screen_t& screen, uint16_t bar_height);


}  // namespace nyla

#endif
