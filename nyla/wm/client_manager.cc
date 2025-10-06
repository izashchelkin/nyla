#include "nyla/wm/client_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <ios>
#include <iterator>
#include <span>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/commons/rect.h"
#include "nyla/layout/layout.h"
#include "nyla/protocols/atoms.h"
#include "nyla/protocols/properties.h"
#include "nyla/protocols/wm_hints.h"
#include "nyla/protocols/wm_protocols.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

static void ClearZoom(WMState& wm_state) {
  GetActiveStack(wm_state).zoom = false;
  wm_state.layout_dirty = true;
}

static void ApplyBorderColor(xcb_connection_t* conn, xcb_window_t window,
                             uint32_t color) {
  if (window) {
    xcb_change_window_attributes(conn, window, XCB_CW_BORDER_PIXEL, &color);
  }
}

static void ApplyNormalBorder(WMState& wm_state) {
  ApplyBorderColor(wm_state.conn, GetActiveWindow(wm_state),
                   wm_state.screen->black_pixel);
}

static void ApplyActiveBorder(WMState& wm_state) {
  ApplyBorderColor(wm_state.conn, GetActiveWindow(wm_state), 0xff5c00);
}

static void MoveActiveBorder(WMState& wm_state, xcb_window_t window) {
  ApplyNormalBorder(wm_state);
  GetActiveStack(wm_state).active_window = window;
  ApplyActiveBorder(wm_state);
}

static void SetInputFocus(WMState& wm_state, xcb_window_t window,
                          xcb_timestamp_t time) {
  if (!window || !wm_state.clients.contains(window)) {
    xcb_set_input_focus(wm_state.conn, XCB_INPUT_FOCUS_POINTER_ROOT,
                        wm_state.screen->root, time);
    return;
  }

  const Client& client = wm_state.clients.at(window);

  xcb_set_input_focus(wm_state.conn, XCB_INPUT_FOCUS_POINTER_ROOT,
                      client.wm_hints_input ? window : wm_state.screen->root,
                      time);

  if (client.wm_take_focus)
    Send_WM_Take_Focus(wm_state.conn, window, wm_state.atoms, time);
}

void CheckFocusTheft(WMState& wm_state) {
  auto reply = xcb_get_input_focus_reply(
      wm_state.conn, xcb_get_input_focus(wm_state.conn), nullptr);
  xcb_window_t window = reply->focus;
  free(reply);

  if (!window) return;

  const xcb_window_t active_client_window = GetActiveWindow(wm_state);

  auto restore_input_focus = [&wm_state, active_client_window] {
    SetInputFocus(
        wm_state, active_client_window,
        XCB_CURRENT_TIME);  // TODO: use SERVERTIME? <-- requires extension
  };

  if (!wm_state.clients.contains(window)) {
    for (;;) {
      xcb_query_tree_reply_t* reply = xcb_query_tree_reply(
          wm_state.conn, xcb_query_tree(wm_state.conn, window), nullptr);

      if (!reply) {
        LOG(ERROR) << "xcb_query_tree_reply failed for " << window;
        restore_input_focus();
        return;
      }

      xcb_window_t parent = reply->parent;
      free(reply);

      if (!parent && parent == wm_state.screen->root) break;
      window = parent;
    }
  }

  if (active_client_window == window) return;

  Client& client = wm_state.clients.at(window);
  if (client.transient_for) {
    if (client.transient_for == active_client_window) return;

    Client& active_client = wm_state.clients.at(window);
    if (client.transient_for == active_client.transient_for) return;
  }

  LOG(WARNING) << "Restoring focus back to " << std::hex << active_client_window
               << " (changed by " << std::hex << window << ")";
  restore_input_focus();
}

static void UpdateClientProperties(WMState& wm_state,
                                   xcb_window_t client_window, Client& client) {
  if (auto wm_hints = FetchProperty<WM_Hints>(
          wm_state.conn, client_window, XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS);
      wm_hints) {
    client.wm_hints_input = wm_hints->input;
  } else {
    client.wm_hints_input = false;
  }

  if (auto wm_normal_hints = FetchProperty<WM_Normal_Hints>(
          wm_state.conn, client_window, XCB_ATOM_WM_NORMAL_HINTS,
          XCB_ATOM_WM_SIZE_HINTS);
      wm_normal_hints) {
    client.max_width = std::max(0, wm_normal_hints->max_width);
    client.max_height = std::max(0, wm_normal_hints->max_height);
  } else {
    client.max_width = 0;
    client.max_height = 0;
  }

  client.wm_delete_window = false;
  client.wm_take_focus = false;
  if (auto wm_protocols = FetchPropertyVector<xcb_atom_t>(
          wm_state.conn, client_window, wm_state.atoms->wm_protocols,
          XCB_ATOM_ATOM);
      wm_protocols) {
    for (xcb_atom_t atom : *wm_protocols) {
      if (atom == wm_state.atoms->wm_delete_window)
        client.wm_delete_window = true;
      if (atom == wm_state.atoms->wm_take_focus) client.wm_take_focus = true;
    }
  }

  if (auto name = FetchPropertyVector<char>(
          wm_state.conn, client_window, wm_state.atoms->wm_name, XCB_ATOM_ANY);
      name) {
    client.name = std::string_view{name->data(), name->size()};
  } else {
    client.name = "";
  }

  client.transient_for = 0;
  if (auto transient_for = FetchProperty<xcb_window_t>(
          wm_state.conn, client_window, XCB_ATOM_WM_TRANSIENT_FOR,
          XCB_ATOM_WINDOW);
      transient_for) {
    auto main_window = *transient_for;
    if (main_window) {
      if (wm_state.clients.contains(main_window)) {
        client.transient_for = main_window;
      } else {
        LOG(WARNING) << "Invalid WM_TRANSIENT_FOR=" << std::hex << main_window
                     << " on " << std::hex << client_window;
      }
    }
  }
}

static xcb_window_t FindNonTransient(WMState& wm_state,
                                     xcb_window_t transient_for) {
  for (int i = 0; i < 10; ++i) {
    auto it = wm_state.clients.find(transient_for);
    if (it == wm_state.clients.end()) break;

    xcb_window_t next = it->second.transient_for;
    if (!next) return transient_for;
    transient_for = next;
  }

  LOG(WARNING) << "Could not find logical parent via WM_TRANSIENT_FOR "
               << transient_for;
  return 0;
}

void ManageClientsStartup(WMState& wm_state) {
  xcb_query_tree_reply_t* tree_reply = xcb_query_tree_reply(
      wm_state.conn, xcb_query_tree(wm_state.conn, wm_state.screen->root),
      nullptr);
  if (!tree_reply) return;

  std::span<xcb_window_t> children = {
      xcb_query_tree_children(tree_reply),
      static_cast<size_t>(xcb_query_tree_children_length(tree_reply))};

  for (xcb_window_t client_window : children) {
    xcb_get_window_attributes_reply_t* attr_reply =
        xcb_get_window_attributes_reply(
            wm_state.conn,
            xcb_get_window_attributes(wm_state.conn, client_window), nullptr);
    if (!attr_reply) continue;
    absl::Cleanup attr_reply_freer = [attr_reply] { free(attr_reply); };

    if (attr_reply->override_redirect) continue;
    if (attr_reply->map_state == XCB_MAP_STATE_UNMAPPED) continue;

    xcb_change_window_attributes(wm_state.conn, client_window,
                                 XCB_CW_EVENT_MASK,
                                 (uint32_t[]){XCB_EVENT_MASK_ENTER_WINDOW,
                                              XCB_EVENT_MASK_PROPERTY_CHANGE});

    wm_state.clients.try_emplace(client_window, Client{});
  }

  for (auto& [client_window, client] : wm_state.clients) {
    UpdateClientProperties(wm_state, client_window, client);
  }

  ClientStack& stack = wm_state.stacks.at(0);

  for (auto& [client_window, client] : wm_state.clients) {
    if (client.transient_for) {
      client.transient_for = FindNonTransient(wm_state, client.transient_for);
      if (client.transient_for) {
        wm_state.clients.at(client.transient_for)
            .subwindows.emplace_back(client_window);
        continue;
      }
    }

    stack.windows.emplace_back(client_window);
  }

  wm_state.layout_dirty = true;

  free(tree_reply);
}

void ManageClient(WMState& wm_state, xcb_window_t client_window, bool focus) {
  if (auto [it, inserted] =
          wm_state.clients.try_emplace(client_window, Client{});
      inserted) {
    wm_state.layout_dirty = true;

    xcb_change_window_attributes(wm_state.conn, client_window,
                                 XCB_CW_EVENT_MASK,
                                 (uint32_t[]){XCB_EVENT_MASK_ENTER_WINDOW,
                                              XCB_EVENT_MASK_PROPERTY_CHANGE});
    auto& [_, client] = *it;
    UpdateClientProperties(wm_state, client_window, client);

    if (client.transient_for) {
      client.transient_for = FindNonTransient(wm_state, client.transient_for);
      if (client.transient_for) {
        wm_state.clients.at(client.transient_for)
            .subwindows.push_back(client_window);
        return;
      }
    }

    ClientStack& stack = GetActiveStack(wm_state);
    stack.windows.emplace_back(client_window);
    ClearZoom(wm_state);

    if (focus) {
      xcb_window_t active_window = GetActiveWindow(wm_state);

      if (active_window != client_window) {
        SetInputFocus(wm_state, client_window, XCB_CURRENT_TIME);
        MoveActiveBorder(wm_state, client_window);
      }
    }
  }
}

void UnmanageClient(WMState& wm_state, xcb_window_t window) {
  auto it = wm_state.clients.find(window);
  if (it == wm_state.clients.end()) return;

  auto& client = it->second;

  if (client.transient_for) {
    CHECK(client.subwindows.empty());
    auto& subwindows = wm_state.clients.at(client.transient_for).subwindows;
    auto it = std::ranges::find(subwindows, window);
    CHECK(it != subwindows.end());
    subwindows.erase(it);
  } else {
    for (xcb_window_t subwindow : client.subwindows) {
      wm_state.clients.at(subwindow).transient_for = 0;
    }
  }

  wm_state.clients.erase(it);

  for (size_t istack = 0; istack < wm_state.stacks.size(); ++istack) {
    ClientStack& stack = wm_state.stacks.at(istack);

    auto it = std::ranges::find_if(
        stack.windows, [window](xcb_window_t elem) { return elem == window; });
    if (it == stack.windows.end()) continue;

    wm_state.layout_dirty = true;
    stack.windows.erase(it);

    if (stack.active_window == window) {
      stack.active_window = 0;

      if (istack == wm_state.active_stack_idx) {
        if (stack.windows.empty()) {
          xcb_set_input_focus(wm_state.conn, XCB_INPUT_FOCUS_POINTER_ROOT,
                              wm_state.screen->root, XCB_CURRENT_TIME);
        } else {
          stack.active_window = stack.windows[0];
          SetInputFocus(wm_state, stack.active_window, XCB_CURRENT_TIME);
          ApplyActiveBorder(wm_state);
        }
      }
    }
    return;
  }
}

static void ConfigureClient(xcb_connection_t* conn, xcb_window_t client_window,
                            Client& client, const Rect& new_rect) {
  uint16_t mask = 0;
  mask |= XCB_CONFIG_WINDOW_X;
  mask |= XCB_CONFIG_WINDOW_Y;
  mask |= XCB_CONFIG_WINDOW_WIDTH;
  mask |= XCB_CONFIG_WINDOW_HEIGHT;
  mask |= XCB_CONFIG_WINDOW_BORDER_WIDTH;
  // mask |= XCB_CONFIG_WINDOW_STACK_MODE;

  struct {
    Rect rect;
    uint32_t border_width;
    // uint32_t stack_mode;
  } values = {new_rect, 2};
  static_assert(sizeof(values) == 5 * 4);

  xcb_configure_window(conn, client_window, mask, &values);
  client.wants_configure_notify = IsSameWH(client.rect, new_rect);
}

void ApplyLayoutChanges(WMState& wm_state, const Rect& screen_rect,
                        uint32_t padding) {
  if (!wm_state.layout_dirty) return;

  auto hide = [&wm_state](xcb_window_t client_window, Client& client) {
    if (client.rect.x() == wm_state.screen->width_in_pixels &&
        client.rect.y() == wm_state.screen->height_in_pixels)
      return;

    client.rect.x() = wm_state.screen->width_in_pixels;
    client.rect.y() = wm_state.screen->height_in_pixels;
    ConfigureClient(wm_state.conn, client_window, client, client.rect);
  };

  auto hide_all = [&wm_state, hide](xcb_window_t client_window,
                                    Client& client) {
    hide(client_window, client);
    for (xcb_window_t subwindow : client.subwindows)
      hide(subwindow, wm_state.clients.at(subwindow));
  };

  auto configure_windows =
      [&wm_state, padding](Rect bounding_rect, std::span<xcb_window_t> windows,
                           LayoutType layout_type, auto visitor) {
        std::vector<Rect> layout =
            ComputeLayout(bounding_rect, windows.size(), padding, layout_type);
        CHECK_EQ(layout.size(), windows.size());

        for (auto [rect, client_window] :
             std::ranges::views::zip(layout, windows)) {
          Client& client = wm_state.clients.at(client_window);

          if (client.max_width) {
            uint32_t new_width = std::min(client.max_width, rect.width());
            rect.x() += (rect.width() - new_width) / 2;
            rect.width() = new_width;
          }
          if (client.max_height) {
            uint32_t new_height = std::min(client.max_height, rect.height());
            rect.y() += (rect.height() - new_height) / 2;
            rect.height() = new_height;
          }

          if (rect != client.rect) {
            ConfigureClient(wm_state.conn, client_window, client, rect);
            client.rect = rect;
          }

          visitor(client);
        }
      };

  auto configure_subwindows = [configure_windows](Client& client) {
    configure_windows(ApplyMargin(client.rect, 20), client.subwindows,
                      LayoutType::kRows, [](Client& client) {});
  };

  ClientStack& stack = GetActiveStack(wm_state);
  if (stack.zoom) {
    for (xcb_window_t client_window : stack.windows) {
      Client& client = wm_state.clients.at(client_window);

      if (client_window != stack.active_window) {
        hide_all(client_window, client);
      } else {
        Rect rect = ApplyPadding(screen_rect, padding);
        if (client.rect != rect) {
          ConfigureClient(wm_state.conn, client_window, client, rect);
          client.rect = rect;
        }

        configure_subwindows(client);
      }
    }
  } else {
    configure_windows(screen_rect, stack.windows, stack.layout_type,
                      configure_subwindows);
  }

  for (size_t istack = 0; istack < wm_state.stacks.size(); ++istack) {
    if (istack == wm_state.active_stack_idx) continue;

    for (xcb_window_t client_window : wm_state.stacks[istack].windows)
      hide_all(client_window, wm_state.clients.at(client_window));
  }

  wm_state.layout_dirty = false;
}

static void ChangeStack(WMState& wm_state, xcb_timestamp_t time,
                        auto compute_idx) {
  ApplyNormalBorder(wm_state);

  wm_state.active_stack_idx =
      compute_idx(wm_state.active_stack_idx + wm_state.stacks.size()) %
      wm_state.stacks.size();

  ApplyActiveBorder(wm_state);

  xcb_window_t window = GetActiveWindow(wm_state);
  if (!window) window = wm_state.screen->root;
  SetInputFocus(wm_state, window, time);

  wm_state.layout_dirty = true;
}

void NextStack(WMState& wm_state, xcb_timestamp_t time) {
  ChangeStack(wm_state, time, [](auto idx) { return idx + 1; });
}

void PrevStack(WMState& wm_state, xcb_timestamp_t time) {
  ChangeStack(wm_state, time, [](auto idx) { return idx - 1; });
}

static void ChangeFocus(WMState& wm_state, xcb_timestamp_t time,
                        auto compute_idx) {
  ClearZoom(wm_state);

  ClientStack& stack = GetActiveStack(wm_state);
  if (stack.windows.empty()) return;

  if (stack.active_window) {
    if (stack.windows.size() < 2) return;

    ApplyNormalBorder(wm_state);

    auto it = std::ranges::find(stack.windows, stack.active_window);
    CHECK_NE(it, stack.windows.end());

    size_t iactive = compute_idx(std::distance(stack.windows.begin(), it) +
                                 stack.windows.size()) %
                     stack.windows.size();
    stack.active_window = stack.windows[iactive];
  } else {
    stack.active_window = stack.windows[0];
  }

  SetInputFocus(wm_state, stack.active_window, time);
  ApplyActiveBorder(wm_state);
}

void NextFocus(WMState& wm_state, xcb_timestamp_t time) {
  ChangeFocus(wm_state, time, [](auto idx) { return idx + 1; });
}

void PrevFocus(WMState& wm_state, xcb_timestamp_t time) {
  ChangeFocus(wm_state, time, [](auto idx) { return idx - 1; });
}

void NextLayout(WMState& wm_state) {
  ClearZoom(wm_state);
  CycleLayoutType(GetActiveStack(wm_state).layout_type);
  wm_state.layout_dirty = true;
}

std::string GetActiveClientBarText(WMState& wm_state) {
  xcb_window_t active_window = GetActiveWindow(wm_state);
  if (!active_window || !wm_state.clients.contains(active_window))
    return "nyla: no active client";

  return wm_state.clients.at(active_window).name;
}

}  // namespace nyla
