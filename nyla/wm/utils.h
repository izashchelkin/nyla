#ifndef NYLA_WM_UTILS_H
#define NYLA_WM_UTILS_H

#include <xcb/xproto.h>

#include <cstdint>

#include "nyla/layout/rect.h"

namespace nyla {

inline Rect AsRect(xcb_screen_t& screen, uint16_t bar_height) {
  return {
      .y = bar_height,
      .width = screen.width_in_pixels,
      .height = static_cast<uint16_t>(screen.height_in_pixels - bar_height)};
}

}  // namespace nyla

#endif
