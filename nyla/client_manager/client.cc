#include "nyla/client_manager/client.h"

#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <utility>

#include "absl/log/check.h"
#include "nyla/layout/layout.h"
#include "nyla/rect/rect.h"

namespace nyla {

void ClientStack::ManageClient(xcb_connection_t* conn, xcb_window_t window) {
  xcb_map_window(conn, window);

  bool is_new = std::ranges::none_of(
      stack_, [window](const auto& client) { return client.window == window; });
  if (is_new) stack_.emplace_back(Client{.window = window});
}

void ClientStack::UnmanageClient(xcb_window_t window) {
  auto it = std::ranges::find_if(
      stack_, [window](const auto& client) { return client.window == window; });
  if (it != stack_.end()) stack_.erase(it);
}

void ClientStack::ApplyLayoutChanges(xcb_connection_t* conn,
                                     const std::vector<Rect>& layout) {
  CHECK_EQ(layout.size(), stack_.size());

  constexpr uint16_t kValueMask =
      XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
      XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH;
  constexpr uint32_t KBorderWidth = 2;

  for (auto&& [rect, client] : std::ranges::views::zip(layout, stack_)) {
    if (rect == client.rect) continue;
    client.rect = rect;

    xcb_configure_window(
        conn, client.window, kValueMask,
        (uint32_t[]){static_cast<uint32_t>(rect.x()),
                     static_cast<uint32_t>(rect.y()), rect.width(),
                     rect.height(), KBorderWidth});
  }
}

void ClientStack::HideAll(xcb_connection_t* conn, xcb_screen_t& screen) {
  constexpr uint16_t kValueMask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;

  for (auto& client : stack_) {
    if (client.rect != Rect{}) {
      client.rect = {};
      xcb_configure_window(
          conn, client.window, kValueMask,
          (uint32_t[]){screen.width_in_pixels, screen.height_in_pixels});
    }
  }
}

void ClientStack::FocusNext(xcb_connection_t* conn,
                            const xcb_screen_t& screen) {
  if (stack_.empty()) {
    focus_idx_ = 0;
    xcb_set_input_focus(conn, XCB_INPUT_FOCUS_PARENT, screen.root,
                        XCB_CURRENT_TIME);
    return;
  }

  if (focus_idx_ < stack_.size()) {
    xcb_change_window_attributes(conn, stack_[focus_idx_].window,
                                 XCB_CW_BORDER_PIXEL, (uint32_t[]){0});
  }

  if (focus_idx_ >= stack_.size() - 1)
    focus_idx_ = 0;
  else
    ++focus_idx_;

  xcb_set_input_focus(conn, XCB_INPUT_FOCUS_PARENT, stack_[focus_idx_].window,
                      XCB_CURRENT_TIME);
  xcb_change_window_attributes(conn, stack_[focus_idx_].window,
                               XCB_CW_BORDER_PIXEL, (uint32_t[]){0xFFFFFFFF});
}

void ClientStack::FocusPrev(xcb_connection_t* conn,
                            const xcb_screen_t& screen) {
  if (stack_.empty()) {
    focus_idx_ = 0;
    xcb_set_input_focus(conn, XCB_INPUT_FOCUS_PARENT, screen.root,
                        XCB_CURRENT_TIME);
    return;
  }

  if (focus_idx_ < stack_.size()) {
    xcb_change_window_attributes(conn, stack_[focus_idx_].window,
                                 XCB_CW_BORDER_PIXEL, (uint32_t[]){0});
  }

  if (focus_idx_ > 0)
    --focus_idx_;
  else
    focus_idx_ = stack_.size() - 1;

  xcb_set_input_focus(conn, XCB_INPUT_FOCUS_PARENT, stack_[focus_idx_].window,
                      XCB_CURRENT_TIME);
  xcb_change_window_attributes(conn, stack_[focus_idx_].window,
                               XCB_CW_BORDER_PIXEL, (uint32_t[]){0xFFFFFFFF});
}

xcb_window_t ClientStack::FocusedWindow() {
  CHECK_LT(focus_idx_, stack_.size());
  return stack_[focus_idx_].window;
}

//
//
//

void ClientStackManager::ManageClient(xcb_connection_t* conn,
                                      xcb_window_t window) {
  CHECK_LT(active_stack_idx_, stacks_.size());

  stacks_[active_stack_idx_].ManageClient(conn, window);
}

void ClientStackManager::UnmanageClient(xcb_window_t window) {
  for (auto& stack : stacks_) stack.UnmanageClient(window);
}

void ClientStackManager::ApplyLayoutChanges(xcb_connection_t* conn,
                                            xcb_screen_t& screen,
                                            const std::vector<Rect>& layout) {
  for (size_t i = 0; i < stacks_.size(); ++i)
    if (i != active_stack_idx_) stacks_[i].HideAll(conn, screen);

  active_stack().ApplyLayoutChanges(conn, layout);
}

void ClientStackManager::NextStack() {
  CHECK_LT(active_stack_idx_, stacks_.size());

  if (active_stack_idx_ < stacks_.size() - 1)
    ++active_stack_idx_;
  else
    active_stack_idx_ = 0;
}

void ClientStackManager::PrevStack() {
  CHECK_LT(active_stack_idx_, stacks_.size());

  if (active_stack_idx_ > 0)
    --active_stack_idx_;
  else
    active_stack_idx_ = stacks_.size() - 1;
}

void ClientStackManager::NextLayout() {
  auto get_next_layout = [](LayoutType current_layout) -> LayoutType {
    switch (current_layout) {
      case LayoutType::kColumns:
        return LayoutType::kRows;
      case LayoutType::kRows:
        return LayoutType::kGrid;
      case LayoutType::kGrid:
        return LayoutType::kColumns;
    }
    std::unreachable();
  };

  LayoutType& layout_type = active_stack().layout_type();
  layout_type = get_next_layout(layout_type);
}

void ClientStackManager::FocusNext(xcb_connection_t* conn,
                                   const xcb_screen_t& screen) {
  active_stack().FocusNext(conn, screen);
}

void ClientStackManager::FocusPrev(xcb_connection_t* conn,
                                   const xcb_screen_t& screen) {
  active_stack().FocusPrev(conn, screen);
}

xcb_window_t ClientStackManager::FocusedWindow() {
  return active_stack().FocusedWindow();
}

}  // namespace nyla
