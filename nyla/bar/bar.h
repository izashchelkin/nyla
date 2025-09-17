#ifndef NYLA_BAR_H
#define NYLA_BAR_H

#include <cstdint>
#include <string_view>

#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

class Bar {
 public:
  Bar(uint16_t height) : height_{height}, window_{}, gc_{} {}

  bool Init(xcb_connection_t *conn, xcb_screen_t &screen);

  void Update(xcb_connection_t *conn, xcb_screen_t &screen,
              std::string_view msg);

  uint16_t height() { return height_; }

 private:
  uint16_t height_;
  xcb_window_t window_;
  xcb_gcontext_t gc_;
};

}  // namespace nyla

#endif
