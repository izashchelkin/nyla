#pragma once

#include "xcb/xproto.h"

class FocusManager {
 public:
  FocusManager() : focused_window_{0} {}

  xcb_window_t& focused_window() { return focused_window_; }
  const xcb_window_t& focused_window() const { return focused_window_; }

 private:
  xcb_window_t focused_window_;
};
