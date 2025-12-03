#pragma once

#include <cstdint>

#include "absl/strings/str_format.h"
#include "xcb/xproto.h"

namespace nyla {

namespace platform_x11_internal {

struct WM_Hints {
  static constexpr uint32_t kInputHint = 1;
  static constexpr uint32_t kStateHint = 1 << 1;
  static constexpr uint32_t kIconPixmapHint = 1 << 2;
  static constexpr uint32_t kIconWindowHint = 1 << 3;
  static constexpr uint32_t kIconPositionHint = 1 << 4;
  static constexpr uint32_t kIconMaskHint = 1 << 5;
  static constexpr uint32_t kWindowGroupHint = 1 << 6;
  static constexpr uint32_t kUrgencyHint = 1 << 8;

  uint32_t flags;
  bool input;
  uint32_t initial_state;
  xcb_pixmap_t icon_pixmap;
  xcb_window_t icon_window;
  int32_t icon_x;
  int32_t icon_y;
  xcb_pixmap_t icon_mask;
  xcb_window_t window_group;

  bool urgent() const {
    return flags & WM_Hints::kUrgencyHint;
  }
};
static_assert(sizeof(WM_Hints) == 9 * 4);

void Initialize(WM_Hints& h);

template <typename Sink>
void AbslStringify(Sink& sink, const WM_Hints& h) {
  absl::Format(&sink,
               "WM_Hints {flags=%v input=%v initial_state=%v icon_pixmap=%v "
               "icon_window=%v icon_x=%v icon_y=%v icon_mask=%v window_group=%v}",
               h.flags, h.input, h.initial_state, h.icon_pixmap, h.icon_window, h.icon_x, h.icon_y, h.icon_mask,
               h.window_group);
}

struct WM_Normal_Hints {
  static constexpr uint32_t kPMinSize = 1 << 4;
  static constexpr uint32_t kPMaxSize = 1 << 5;
  static constexpr uint32_t kPResizeInc = 1 << 6;
  static constexpr uint32_t kPAspect = 1 << 7;
  static constexpr uint32_t kPBaseSize = 1 << 8;
  static constexpr uint32_t kPWinGravity = 1 << 9;

  uint32_t flags;
  uint32_t pad[4];
  int32_t min_width, min_height;
  int32_t max_width, max_height;
  int32_t width_inc, height_inc;
  struct {
    int num;
    int den;
  } min_aspect, max_aspect;
  int32_t base_width, base_height;
  xcb_gravity_t win_gravity;
};
static_assert(sizeof(WM_Normal_Hints) == 18 * 4);

void Initialize(WM_Normal_Hints& h);

template <typename Sink>
void AbslStringify(Sink& sink, const WM_Normal_Hints& h) {
  absl::Format(&sink,
               "WM_Normal_Hints {flags=%v min_width=%v min_height=%v "
               "max_width=%v max_height=%v width_inc=%v "
               "height_inc=%v min_aspect=%v/%v max_aspect=%v/%v base_width=%v "
               "base_height=%v "
               "win_gravity=%v}",
               h.flags, h.min_width, h.min_height, h.max_width, h.max_height, h.width_inc, h.height_inc,
               h.min_aspect.num, h.min_aspect.den, h.max_aspect.num, h.max_aspect.den, h.base_width, h.base_height,
               h.win_gravity);
}

}  // namespace platform_x11_internal

}  // namespace nyla