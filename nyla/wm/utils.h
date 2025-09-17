#ifndef NYLA_WM_UTILS_H
#define NYLA_WM_UTILS_H

#include <xcb/xproto.h>

#include <cstdint>

#include "nyla/layout/rect.h"

namespace nyla {

Rect AsRect(xcb_screen_t& screen, uint16_t bar_height);

}  // namespace nyla

#endif
