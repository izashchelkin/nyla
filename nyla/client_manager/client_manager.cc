#include "nyla/client_manager/client_manager.h"

#include <cstddef>

#include "nyla/client_manager/client_stack.h"
#include "nyla/layout/layout.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

void ClientManager::ManageClient(xcb_connection_t* conn, xcb_window_t window) {
  xcb_map_window(conn, window);

  bool is_new = std::ranges::none_of(
      stack().clients,
      [window](const auto& client) { return client.window == window; });
  if (is_new) stack().clients.emplace_back(Client{.window = window});
}

void ClientManager::UnmanageClient(xcb_window_t window) {
  for (auto& stack : stacks_) {
    auto it = std::ranges::find_if(stack.clients, [window](const auto& client) {
      return client.window == window;
    });
    if (it != stack.clients.end()) stack.clients.erase(it);
  }
}

void ClientManager::ApplyLayoutChanges(xcb_connection_t* conn,
                                       xcb_screen_t& screen,
                                       const std::vector<Rect>& layout) {
  CHECK_EQ(layout.size(), stack().clients.size());

  for (const auto& [rect, client] :
       std::ranges::views::zip(layout, stack().clients)) {
    if (rect != client.rect) {
      client.rect = rect;

      xcb_configure_window(
          conn, client.window,
          XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
              XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH,
          (uint32_t[]){static_cast<uint32_t>(rect.x()),
                       static_cast<uint32_t>(rect.y()), rect.width(),
                       rect.height(), 2});
    }
  }

  for (size_t i = 0; i < stacks_.size(); ++i) {
    if (i != istack_) {
      for (auto& client : stacks_[i].clients) {
        if (client.rect != Rect{}) {
          client.rect = Rect{};
          xcb_configure_window(
              conn, client.window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
              (uint32_t[]){screen.width_in_pixels, screen.height_in_pixels});
        }
      }
    }
  }
}

void ClientManager::NextStack() {
  CHECK_LT(istack_, stacks_.size());
  istack_ = (istack_ + 1) % stacks_.size();
}

void ClientManager::PrevStack() {
  CHECK_LT(istack_, stacks_.size());
  istack_ = (istack_ - 1) % stacks_.size();
}

void ClientManager::NextLayout() { CycleLayoutType(stack().layout_type); }

static void FocusInternal(xcb_connection_t* conn, const xcb_screen_t& screen,
                          ClientStack& stack, size_t idelta) {
  if (stack.clients.empty()) {
    stack.ifocus = 0;
    xcb_set_input_focus(conn, XCB_INPUT_FOCUS_PARENT, screen.root,
                        XCB_CURRENT_TIME);
    return;
  }

  size_t old_ifocus = stack.ifocus;
  stack.ifocus = (stack.ifocus + idelta) % stack.clients.size();

  if (old_ifocus != stack.ifocus && old_ifocus < stack.clients.size()) {
    xcb_change_window_attributes(conn, stack.clients[old_ifocus].window,
                                 XCB_CW_BORDER_PIXEL,
                                 (uint32_t[]){screen.black_pixel});
  }

  xcb_window_t focused_window = stack.clients[stack.ifocus].window;
  xcb_set_input_focus(conn, XCB_INPUT_FOCUS_PARENT, focused_window,
                      XCB_CURRENT_TIME);
  xcb_change_window_attributes(conn, focused_window, XCB_CW_BORDER_PIXEL,
                               (uint32_t[]){screen.white_pixel});
}

void ClientManager::RegainFocus(xcb_connection_t* conn,
                                const xcb_screen_t& screen) {
  FocusInternal(conn, screen, stack(), 0);
}

void ClientManager::FocusNext(xcb_connection_t* conn,
                              const xcb_screen_t& screen) {
  FocusInternal(conn, screen, stack(), 1);
}

void ClientManager::FocusPrev(xcb_connection_t* conn,
                              const xcb_screen_t& screen) {
  FocusInternal(conn, screen, stack(), -1);
}

xcb_window_t ClientManager::GetFocusedWindow() {
  CHECK_LT(stack().ifocus, stack().clients.size());
  return stack().clients[stack().ifocus].window;
}

}  // namespace nyla
