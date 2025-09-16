#ifndef NYLA_CLIENT_H
#define NYLA_CLIENT_H

#include "nyla/layout/rect.h"
#include "xcb/xproto.h"

namespace nyla {

struct Client {
  xcb_window_t window;
  Rect rect;
};

class ClientStack {
 public:
  void HandleMapRequest(xcb_connection_t* conn, xcb_window_t window);
  void HandleUnmapNotify(xcb_window_t window);
  void ApplyLayoutChanges(xcb_connection_t* conn,
                          const std::vector<Rect>& layout);

  auto size() const { return stack_.size(); }

 private:
  std::vector<Client> stack_;
};

}  // namespace nyla

#endif
