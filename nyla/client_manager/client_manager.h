#pragma once

#include <cstddef>

#include "absl/log/check.h"
#include "nyla/client_manager/client_stack.h"
#include "nyla/layout/layout.h"
#include "nyla/rect/rect.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

class ClientManager {
 public:
  ClientManager() : stacks_{9}, istack_{0} {}

  void ManageClient(xcb_connection_t* conn, xcb_window_t window);
  void UnmanageClient(xcb_window_t window);

  void ApplyLayoutChanges(xcb_connection_t* conn, xcb_screen_t& screen,
                          const std::vector<Rect>& layout);

  void NextStack();
  void PrevStack();

  void NextLayout();

  void MoveFocus(xcb_connection_t* conn, const xcb_screen_t& screen,
                 ssize_t idelta = 0);
  void FocusWindow(xcb_connection_t* conn, const xcb_screen_t& screen,
                   xcb_window_t window);
  xcb_window_t GetFocusedWindow();

  size_t size() const { return stack().clients.size(); }
  LayoutType layout_type() const { return stack().layout_type; }

 private:
  ClientStack& stack() {
    CHECK_LT(istack_, stacks_.size());
    return stacks_[istack_];
  }
  const ClientStack& stack() const {
    CHECK_LT(istack_, stacks_.size());
    return stacks_[istack_];
  }

  std::vector<ClientStack> stacks_;
  size_t istack_;
};

}  // namespace nyla
