#include "nyla/wm/client.h"

#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/layout/layout.h"

namespace nyla {

void ClientStack::HandleMapRequest(xcb_connection_t* conn,
                                   xcb_window_t window) {
  xcb_map_window(conn, window);

  bool is_new = std::ranges::none_of(
      stack_, [window](const auto& client) { return client.window == window; });
  if (is_new) stack_.emplace_back(Client{.window = window});
}

void ClientStack::HandleUnmapNotify(xcb_window_t window) {
  auto it = std::ranges::find_if(
      stack_, [window](const auto& client) { return client.window == window; });
  if (it != stack_.end()) stack_.erase(it);
}

void ClientStack::ApplyLayoutChanges(xcb_connection_t* conn,
                                     const std::vector<Rect>& layout) {
  CHECK_EQ(layout.size(), stack_.size());

  constexpr uint16_t kValueMask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                                  XCB_CONFIG_WINDOW_WIDTH |
                                  XCB_CONFIG_WINDOW_HEIGHT;

  size_t affected = 0;
  for (auto&& [rect, client] : std::ranges::views::zip(layout, stack_)) {
    if (rect == client.rect) continue;
    client.rect = rect;

    auto value_list =
        std::to_array({static_cast<uint32_t>(rect.x),
                       static_cast<uint32_t>(rect.y), rect.width, rect.height});
    xcb_configure_window(conn, client.window, kValueMask, value_list.data());

    ++affected;
  }

  if (affected > 0)
    LOG(INFO) << "applied layout changes to " << affected << " clients";
}

void ClientStack::MoveOffScreen(xcb_connection_t* conn) {
  constexpr uint16_t kValueMask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;

  for (auto& client : stack_) {
    if (client.rect != Rect{}) {
      client.rect = {};
      xcb_configure_window(conn, client.window, kValueMask,
                           (uint32_t[]){32000, 32000});
    }
  }
}

void ClientStackManager::HandleMapRequest(xcb_connection_t* conn,
                                          xcb_window_t window) {
  CHECK_LT(active_stack_idx_, stacks_.size());

  stacks_[active_stack_idx_].HandleMapRequest(conn, window);
}

void ClientStackManager::HandleUnmapNotify(xcb_window_t window) {
  for (auto& stack : stacks_) stack.HandleUnmapNotify(window);
}

void ClientStackManager::ApplyLayoutChanges(xcb_connection_t* conn,
                                            const std::vector<Rect>& layout) {
  active_stack().ApplyLayoutChanges(conn, layout);

  for (size_t i = 0; i < stacks_.size(); ++i) {
    if (i != active_stack_idx_) stacks_[i].MoveOffScreen(conn);
  }
}

void ClientStackManager::NextStack() {
  CHECK_LT(active_stack_idx_, stacks_.size());

  if (active_stack_idx_ < stacks_.size() - 1) ++active_stack_idx_;
}

void ClientStackManager::PrevStack() {
  CHECK_LT(active_stack_idx_, stacks_.size());

  if (active_stack_idx_ > 0) --active_stack_idx_;
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
    CHECK(false);
  };

  LayoutType& layout_type = active_stack().layout_type();
  layout_type = get_next_layout(layout_type);
}

}  // namespace nyla
