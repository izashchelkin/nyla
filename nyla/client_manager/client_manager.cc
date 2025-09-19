#include "nyla/client_manager/client_manager.h"

#include <cstddef>
#include <cstdint>

#include "nyla/client_manager/client_stack.h"
#include "nyla/layout/layout.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

void ClientManager::ManageClient(xcb_connection_t* conn, xcb_window_t window) {
  xcb_map_window(conn, window);
  xcb_change_window_attributes(conn, window, XCB_CW_EVENT_MASK,
                               (uint32_t[]){XCB_EVENT_MASK_ENTER_WINDOW});

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
  istack_ =
      (static_cast<ssize_t>(istack_) - 1 + stacks_.size()) % stacks_.size();
}

void ClientManager::NextLayout() { CycleLayoutType(stack().layout_type); }

void ClientManager::MoveFocus(xcb_connection_t* conn,
                              const xcb_screen_t& screen, ssize_t idelta) {
  if (stack().clients.empty()) {
    stack().focused = {};
    xcb_set_input_focus(conn, XCB_INPUT_FOCUS_PARENT, screen.root,
                        XCB_CURRENT_TIME);
  } else {
    ClientIndexUid old_focused = stack().focused;
    bool changed = false;

    bool old_accessible = old_focused.index < stack().clients.size();
    if (old_focused.uid && old_accessible) {
      stack().focused.index = (static_cast<ssize_t>(stack().focused.index) +
                               idelta + stack().clients.size()) %
                              stack().clients.size();

      stack().focused.uid = stack().clients[stack().focused.index].uid;
      changed = stack().focused.uid != old_focused.uid;
    } else {
      stack().focused.index = 0;
      stack().focused.uid = stack().clients[0].uid;
      changed = true;
    }

    if (changed && old_accessible) {
      xcb_change_window_attributes(
          conn, stack().clients[old_focused.index].window, XCB_CW_BORDER_PIXEL,
          (uint32_t[]){screen.black_pixel});
    }

    if (changed || !idelta) {
      xcb_window_t focused_window =
          stack().clients[stack().focused.index].window;
      xcb_set_input_focus(conn, XCB_INPUT_FOCUS_PARENT, focused_window,
                          XCB_CURRENT_TIME);
      xcb_change_window_attributes(conn, focused_window, XCB_CW_BORDER_PIXEL,
                                   (uint32_t[]){screen.white_pixel});
    }
  }
}

void ClientManager::FocusWindow(xcb_connection_t* conn,
                                const xcb_screen_t& screen,
                                xcb_window_t window) {
  xcb_window_t old_focused_window =
      stack().clients[stack().focused.index].window;
  if (old_focused_window == window) return;

  for (size_t i = 0; i < stack().clients.size(); ++i) {
    auto& client = stack().clients[i];
    if (client.window == window) {
      xcb_change_window_attributes(conn, old_focused_window,
                                   XCB_CW_BORDER_PIXEL,
                                   (uint32_t[]){screen.black_pixel});

      stack().focused.index = i;
      stack().focused.uid = client.uid;

      xcb_set_input_focus(conn, XCB_INPUT_FOCUS_PARENT, client.window,
                          XCB_CURRENT_TIME);
      xcb_change_window_attributes(conn, client.window, XCB_CW_BORDER_PIXEL,
                                   (uint32_t[]){screen.white_pixel});
      return;
    }
  }
}

xcb_window_t ClientManager::GetFocusedWindow() {
  if (!stack().focused.uid) return 0;

  CHECK_LT(stack().focused.index, stack().clients.size());
  return stack().clients[stack().focused.index].window;
}

}  // namespace nyla
