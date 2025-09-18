#pragma once

#include <cstddef>

#include "nyla/layout/layout.h"
#include "nyla/uid/uid.h"
#include "xcb/xproto.h"

namespace nyla {

struct Client {
  Uid uid = NextUid();
  xcb_window_t window;
  Rect rect;
};

struct ClientIndexUid {
  size_t index;
  Uid uid;
};

struct ClientStack {
  std::vector<Client> clients;
  ClientIndexUid focused;
  LayoutType layout_type;
};

}  // namespace nyla
