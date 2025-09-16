#include "nyla/wm/client.h"

#include <xcb/xcb.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>

#include "absl/log/check.h"
#include "absl/log/log.h"

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
  CHECK(layout.size() == stack_.size());

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

}  // namespace nyla
