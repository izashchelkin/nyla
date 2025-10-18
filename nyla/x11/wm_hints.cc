#include "nyla/x11/wm_hints.h"

namespace nyla {

void Initialize(WM_Hints& h) {
  if (!(h.flags & WM_Hints::kInputHint)) {
    h.input = true;
  }

  if (!(h.flags & WM_Hints::kStateHint)) {
    h.initial_state = 0;
  }

  if (!(h.flags & WM_Hints::kIconPixmapHint)) {
    h.icon_pixmap = 0;
  }

  if (!(h.flags & WM_Hints::kIconWindowHint)) {
    h.icon_window = 0;
  }

  if (!(h.icon_x & WM_Hints::kIconPositionHint)) {
    h.icon_x = 0;
    h.icon_y = 0;
  }

  if (!(h.icon_x & WM_Hints::kIconMaskHint)) {
    h.icon_mask = 0;
  }
}

void Initialize(WM_Normal_Hints& h) {
  if (!(h.flags & WM_Normal_Hints::kPMinSize)) {
    h.min_width = 0;
    h.min_height = 0;
  }

  if (!(h.flags & WM_Normal_Hints::kPMaxSize)) {
    h.max_width = 0;
    h.max_height = 0;
  }

  if (!(h.flags & WM_Normal_Hints::kPResizeInc)) {
    h.width_inc = 0;
    h.height_inc = 0;
  }

  if (!(h.flags & WM_Normal_Hints::kPAspect)) {
    h.min_aspect = {};
    h.max_aspect = {};
  }

  if (!(h.flags & WM_Normal_Hints::kPBaseSize)) {
    h.base_width = 0;
    h.base_height = 0;
  }

  if (!(h.flags & WM_Normal_Hints::kPWinGravity)) {
    h.win_gravity = {};
  }
}

}  // namespace nyla
