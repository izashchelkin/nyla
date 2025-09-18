#ifndef NYLA_WM_BAR_MANAGER_H
#define NYLA_WM_BAR_MANAGER_H

#include <cstdint>

#include "nyla/bar/bar.h"

namespace nyla {

class BarManager {
 public:
  BarManager() : bar_{24} {}

  bool Init(xcb_connection_t *conn, xcb_screen_t &screen);
  void Update(xcb_connection_t *conn, xcb_screen_t &screen);

  uint16_t height() { return bar_.height(); }

 private:
  Bar bar_;
};

}  // namespace nyla

#endif
