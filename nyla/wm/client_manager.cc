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
#include "nyla/wm/palette.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

static void ClearZoom(WMState& wm_state, ClientStack& stack) {
  if (stack.zoom) {
    stack.zoom = false;
    wm_state.layout_dirty = true;
  }
}

static void ApplyBorder(xcb_connection_t* conn, xcb_window_t window,
                        Color color) {
  if (window)
    xcb_change_window_attributes(conn, window, XCB_CW_BORDER_PIXEL, &color);
}

static void ApplyBorderDefault(WMState& wm_state, const ClientStack& stack) {
  ApplyBorder(wm_state.conn, stack.active_window, Color::kDefault);
}

static void ApplyBorderActive(WMState& wm_state, const ClientStack& stack) {
  ApplyBorder(wm_state.conn, stack.active_window,
              wm_state.follow ? Color::kActiveFollow : Color::kActive);
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

  const ClientStack& stack = GetActiveStack(wm_state);
  const xcb_window_t active_client_window = stack.active_window;

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
    ClearZoom(wm_state, stack);
    stack.windows.emplace_back(client_window);

    if (focus && stack.active_window != client_window) {
      ApplyBorderDefault(wm_state, stack);
      stack.active_window = client_window;
      SetInputFocus(wm_state, stack.active_window, XCB_CURRENT_TIME);
      ApplyBorderActive(wm_state, stack);
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
          ApplyBorderActive(wm_state, stack);
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

          auto center = [](uint32_t max, uint32_t& w, int32_t& x) {
            if (max) {
              uint32_t tmp = std::min(max, w);
              x += (w - tmp) / 2;
              w = tmp;
            }
          };
          center(client.max_width, rect.width(), rect.x());
          center(client.max_height, rect.height(), rect.y());

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
    if (istack != wm_state.active_stack_idx) {
      for (xcb_window_t client_window : wm_state.stacks[istack].windows)
        hide_all(client_window, wm_state.clients.at(client_window));
    }
  }

  wm_state.layout_dirty = false;
}

static void MoveStack(WMState& wm_state, xcb_timestamp_t time,
                      auto compute_idx) {
  size_t iold = wm_state.active_stack_idx;
  size_t inew =
      compute_idx(iold + wm_state.stacks.size()) % wm_state.stacks.size();

  if (iold == inew) return;

  ClientStack& oldstack = GetActiveStack(wm_state);
  wm_state.active_stack_idx = inew;
  ClientStack& newstack = GetActiveStack(wm_state);

  if (wm_state.follow) {
    if (oldstack.active_window) {
      newstack.active_window = oldstack.active_window;
      newstack.windows.emplace_back(oldstack.active_window);

      auto it = std::ranges::find(oldstack.windows, oldstack.active_window);
      CHECK_NE(it, oldstack.windows.end());
      oldstack.windows.erase(it);

      if (oldstack.windows.empty())
        oldstack.active_window = 0;
      else
        oldstack.active_window = oldstack.windows.at(0);
    }
  } else {
    ApplyBorderDefault(wm_state, oldstack);

    xcb_window_t window = newstack.active_window;
    if (!window) window = wm_state.screen->root;
    SetInputFocus(wm_state, window, time);

    ApplyBorderActive(wm_state, newstack);
  }

  wm_state.layout_dirty = true;
}

void MoveStackNext(WMState& wm_state, xcb_timestamp_t time) {
  MoveStack(wm_state, time, [](auto idx) { return idx + 1; });
}

void MoveStackPrev(WMState& wm_state, xcb_timestamp_t time) {
  MoveStack(wm_state, time, [](auto idx) { return idx - 1; });
}

static void MoveLocal(WMState& wm_state, xcb_timestamp_t time,
                      auto compute_idx) {
  ClientStack& stack = GetActiveStack(wm_state);
  ClearZoom(wm_state, stack);

  if (stack.windows.empty()) return;

  if (stack.active_window && stack.windows.size() < 2) {
    return;
  }

  if (stack.active_window) {
    auto it = std::ranges::find(stack.windows, stack.active_window);
    CHECK_NE(it, stack.windows.end());

    size_t iold = std::distance(stack.windows.begin(), it);
    size_t inew =
        compute_idx(iold + stack.windows.size()) % stack.windows.size();

    if (iold == inew) return;

    if (wm_state.follow) {
      std::iter_swap(stack.windows.begin() + iold,
                     stack.windows.begin() + inew);
      wm_state.layout_dirty = true;
    } else {
      ApplyBorderDefault(wm_state, stack);
      stack.active_window = stack.windows.at(inew);
      ApplyBorderActive(wm_state, stack);
      SetInputFocus(wm_state, stack.active_window, time);
    }
  } else {
    if (!wm_state.follow) {
      stack.active_window = stack.windows.at(0);
      ApplyBorderActive(wm_state, stack);
      SetInputFocus(wm_state, stack.active_window, time);
    }
  }
}

void MoveLocalNext(WMState& wm_state, xcb_timestamp_t time) {
  MoveLocal(wm_state, time, [](auto idx) { return idx + 1; });
}

void MoveLocalPrev(WMState& wm_state, xcb_timestamp_t time) {
  MoveLocal(wm_state, time, [](auto idx) { return idx - 1; });
}

void NextLayout(WMState& wm_state) {
  ClientStack& stack = GetActiveStack(wm_state);
  CycleLayoutType(stack.layout_type);
  wm_state.layout_dirty = true;
  ClearZoom(wm_state, stack);
}

std::string GetActiveClientBarText(WMState& wm_state) {
  const ClientStack& stack = GetActiveStack(wm_state);
  xcb_window_t active_window = stack.active_window;
  if (!active_window || !wm_state.clients.contains(active_window))
    return "nyla: no active client";

  return wm_state.clients.at(active_window).name;
}

void CloseActive(WMState& wm_state) {
  ClientStack& stack = GetActiveStack(wm_state);
  if (stack.active_window)
    Send_WM_Delete_Window(wm_state.conn, stack.active_window, wm_state.atoms);
}

void ToggleZoom(WMState& wm_state) {
  ClientStack& stack = GetActiveStack(wm_state);
  stack.zoom ^= 1;
  if (wm_state.follow) {
    wm_state.follow = false;
    ApplyBorderActive(wm_state, stack);
  }
  wm_state.layout_dirty = true;
}

void ToggleFollow(WMState& wm_state) {
  ClientStack& stack = GetActiveStack(wm_state);
  if (wm_state.follow) {
    ClearZoom(wm_state, stack);
    wm_state.follow = false;
  } else {
    wm_state.follow = true;
  }
  ApplyBorderActive(wm_state, stack);
}

}  // namespace nyla
