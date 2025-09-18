#pragma once

#include "nyla/layout/layout.h"
#include "xcb/xproto.h"

namespace nyla {

struct Client {
  xcb_window_t window;
  Rect rect;
};

struct ClientStack {
  std::vector<Client> clients;
  size_t ifocus;
  LayoutType layout_type;
};

}  // namespace nyla
