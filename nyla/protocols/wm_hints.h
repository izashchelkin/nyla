#pragma once

#include <cstdint>

#include "absl/strings/str_format.h"
#include "xcb/xproto.h"

namespace nyla {

struct WM_Hints {
  uint32_t flags;
  bool input;
  uint32_t initial_state;
  xcb_pixmap_t icon_pixmap;
  xcb_window_t icon_window;
  int32_t icon_x;
  int32_t icon_y;
  xcb_pixmap_t icon_mask;
  xcb_window_t window_group;
};
static_assert(sizeof(WM_Hints) == 36);

template <typename Sink>
void AbslStringify(Sink& sink, const WM_Hints& wm_hints) {
  absl::Format(
      &sink,
      "WM_Hints {flags=%v input=%v initial_state=%v icon_pixmap=%v "
      "icon_window=%v icon_x=%v icon_y=%v icon_mask=%v window_group=%v}",
      wm_hints.flags, wm_hints.input, wm_hints.initial_state,
      wm_hints.icon_pixmap, wm_hints.icon_window, wm_hints.icon_x,
      wm_hints.icon_y, wm_hints.icon_mask, wm_hints.window_group);
}

}  // namespace nyla
