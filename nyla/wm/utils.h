#ifndef NYLA_WM_UTILS_H
#define NYLA_WM_UTILS_H

#include <xcb/xproto.h>

#include "nyla/layout/rect.h"

namespace nyla {

inline Rect AsRect(xcb_screen_t& screen) {
  return {.width = screen.width_in_pixels, .height = screen.height_in_pixels};
}

}  // namespace nyla

#endif
