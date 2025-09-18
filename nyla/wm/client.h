#ifndef NYLA_CLIENT_H
#define NYLA_CLIENT_H

#include <cstddef>

#include "absl/log/check.h"
#include "nyla/layout/layout.h"
#include "nyla/layout/rect.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

struct Client {
  xcb_window_t window;
  Rect rect;
};

class ClientStack {
 public:
  ClientStack() : stack_{}, focus_idx_{0}, layout_type_{LayoutType::kGrid} {}

  void ManageClient(xcb_connection_t* conn, xcb_window_t window);
  void UnmanageClient(xcb_window_t window);
  void ApplyLayoutChanges(xcb_connection_t* conn,
                          const std::vector<Rect>& layout);
  void HideAll(xcb_connection_t* conn, xcb_screen_t& screen);

  void FocusNext(xcb_connection_t* conn, const xcb_screen_t& screen);
  void FocusPrev(xcb_connection_t* conn, const xcb_screen_t& screen);

  xcb_window_t FocusedWindow();

  size_t size() const { return stack_.size(); }

  LayoutType& layout_type() { return layout_type_; };
  const LayoutType& layout_type() const { return layout_type_; };

 private:
  std::vector<Client> stack_;
  size_t focus_idx_;
  LayoutType layout_type_;
};

class ClientStackManager {
 public:
  ClientStackManager() : stacks_{9}, active_stack_idx_{0} {}

  void ManageClient(xcb_connection_t* conn, xcb_window_t window);
  void UnmanageClient(xcb_window_t window);
  void ApplyLayoutChanges(xcb_connection_t* conn, xcb_screen_t& screen,
                          const std::vector<Rect>& layout);

  void NextStack();
  void PrevStack();

  void NextLayout();

  void FocusNext(xcb_connection_t* conn, const xcb_screen_t& screen);
  void FocusPrev(xcb_connection_t* conn, const xcb_screen_t& screen);

  xcb_window_t FocusedWindow();

  auto size() const { return active_stack().size(); }
  auto layout_type() const { return active_stack().layout_type(); }

 private:
  ClientStack& active_stack() {
    CHECK_LT(active_stack_idx_, stacks_.size());
    return stacks_[active_stack_idx_];
  }
  const ClientStack& active_stack() const {
    CHECK_LT(active_stack_idx_, stacks_.size());
    return stacks_[active_stack_idx_];
  }

  std::vector<ClientStack> stacks_;
  size_t active_stack_idx_;
};

}  // namespace nyla

#endif
