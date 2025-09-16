#ifndef NYLA_CLIENT_H
#define NYLA_CLIENT_H

#include "nyla/layout/rect.h"
#include "xcb/xproto.h"

namespace nyla {

struct Client {
  xcb_window_t window;
  Rect rect;
};

}  // namespace nyla

#endif
