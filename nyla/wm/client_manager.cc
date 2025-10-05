#include "nyla/wm/client_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
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
  ApplyBorderColor(wm_state.conn, GetActiveWindow(wm_state),
                   wm_state.screen->white_pixel);
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
                      client.input ? window : wm_state.screen->root, time);

  if (client.wm_take_focus)
    Send_WM_Take_Focus(wm_state.conn, window, wm_state.atoms, time);
}

void CheckFocusTheft(WMState& wm_state) {
  auto reply = xcb_get_input_focus_reply(
      wm_state.conn, xcb_get_input_focus(wm_state.conn), nullptr);
  xcb_window_t window = reply->focus;
  free(reply);

  if (!wm_state.clients.contains(window)) {
    for (;;) {
      xcb_query_tree_reply_t* reply = xcb_query_tree_reply(
          wm_state.conn, xcb_query_tree(wm_state.conn, window), nullptr);
      if (!reply) {
        // LOG(ERROR) << "??? null reply";
        break;
      }

      absl::Cleanup reply_freer = [reply] { free(reply); };
      if (reply->parent == reply->root) break;
      window = reply->parent;
    }
  }

  // LOG(INFO) << "focusin " << active_client_window << " =?= " << window;

  xcb_window_t active_client_window = GetActiveWindow(wm_state);
  if (active_client_window != window)
    SetInputFocus(wm_state, active_client_window, XCB_CURRENT_TIME);
}

static bool UpdateClientProperties(WMState& wm_state,
                                   xcb_window_t client_window, Client& client) {
  auto wm_hints = FetchProperty<WM_Hints>(wm_state.conn, client_window,
                                          XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS);
  if (!wm_hints) return false;

  auto wm_protocols = FetchPropertyVector<xcb_atom_t>(
      wm_state.conn, client_window, wm_state.atoms->wm_protocols,
      XCB_ATOM_ATOM);
  if (!wm_protocols) return false;

  client.input = wm_hints->input;

  client.wm_delete_window = false;
  client.wm_take_focus = false;

  for (xcb_atom_t atom : *wm_protocols) {
    if (atom == wm_state.atoms->wm_delete_window)
      client.wm_delete_window = true;
    if (atom == wm_state.atoms->wm_take_focus) client.wm_take_focus = true;
  }

  auto name = FetchPropertyVector<char>(wm_state.conn, client_window,
                                        wm_state.atoms->wm_name, XCB_ATOM_ANY);
  if (!name) return false;

  client.name = std::string_view{name->data(), name->size()};

  auto transient_for = FetchProperty<xcb_window_t>(
      wm_state.conn, client_window, XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW);

  if (transient_for && *transient_for) {
    if (wm_state.clients.contains(*transient_for)) {
      client.transient_for = *transient_for;
    } else {
      LOG(WARNING) << "invalid transient_for " << *transient_for;
    }
  }

  return true;
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

  LOG(WARNING) << "could not find non-transient_for " << transient_for;
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

  free(tree_reply);
}

void ManageClient(WMState& wm_state, xcb_window_t client_window, bool focus) {
  if (auto [it, inserted] =
          wm_state.clients.try_emplace(client_window, Client{});
      inserted) {
    xcb_change_window_attributes(wm_state.conn, client_window,
                                 XCB_CW_EVENT_MASK,
                                 (uint32_t[]){XCB_EVENT_MASK_ENTER_WINDOW,
                                              XCB_EVENT_MASK_PROPERTY_CHANGE});
    auto& [_, client] = *it;

    if (!UpdateClientProperties(wm_state, client_window, client)) {
      LOG(WARNING) << "window gonsky?";
      wm_state.clients.erase(client_window);
      return;
    }

    ClientStack& stack = GetActiveStack(wm_state);

    if (client.transient_for) {
      client.transient_for = FindNonTransient(wm_state, client.transient_for);
      if (client.transient_for) {
        wm_state.clients.at(client.transient_for)
            .subwindows.push_back(client_window);
        return;
      }
    }

    stack.windows.emplace_back(client_window);

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
          ApplyBorderColor(wm_state.conn, stack.active_window,
                           wm_state.screen->white_pixel);
        }
      }
    }
    return;
  }
}

static void ConfigureClient(xcb_connection_t* conn, xcb_window_t client_window,
                            Client& client, const Rect& new_rect, bool above) {
  uint16_t mask = 0;
  mask |= XCB_CONFIG_WINDOW_X;
  mask |= XCB_CONFIG_WINDOW_Y;
  mask |= XCB_CONFIG_WINDOW_WIDTH;
  mask |= XCB_CONFIG_WINDOW_HEIGHT;
  mask |= XCB_CONFIG_WINDOW_BORDER_WIDTH;
  if (above) mask |= XCB_CONFIG_WINDOW_STACK_MODE;

  struct {
    Rect rect;
    uint32_t border_width;
    uint32_t stack_mode;
  } values = {new_rect, 2, XCB_STACK_MODE_ABOVE};
  static_assert(sizeof(values) == 6 * 4);

  xcb_configure_window(conn, client_window, mask, &values);
  client.wants_configure_notify = IsSameWH(client.rect, new_rect);
}

void ApplyLayoutChanges(WMState& wm_state, const Rect& screen_rect,
                        uint32_t padding) {
  ClientStack& stack = GetActiveStack(wm_state);
  std::vector<Rect> layout = ComputeLayout(screen_rect, stack.windows.size(),
                                           padding, stack.layout_type);
  CHECK_EQ(layout.size(), stack.windows.size());

  for (auto [layout_rect, client_window] :
       std::ranges::views::zip(layout, stack.windows)) {
    Client& client = wm_state.clients.at(client_window);

    Rect rect = (client_window == stack.active_window && stack.zoom)
                    ? ApplyPadding(screen_rect, padding)
                    : layout_rect;

    bool force_reconfigure_subwindows = false;
    if (rect != client.rect) {
      ConfigureClient(wm_state.conn, client_window, client, rect, true);
      client.rect = rect;
      force_reconfigure_subwindows = true;
    }

    std::vector<Rect> sublayout =
        ComputeLayout(ApplyMargin(rect, 10), client.subwindows.size(), padding,
                      LayoutType::kGrid);
    for (auto [sublayout_rect, subwindow] :
         std::ranges::views::zip(sublayout, client.subwindows)) {
      Client& subclient = wm_state.clients.at(subwindow);
      if (force_reconfigure_subwindows || sublayout_rect != subclient.rect) {
        ConfigureClient(wm_state.conn, subwindow, subclient, sublayout_rect,
                        true);
        client.rect = rect;
      }
    }
  }

  auto hide = [&wm_state](xcb_window_t client_window, Client& client) {
    if (client.rect != Rect{}) {
      Rect new_rect = client.rect;
      new_rect.x() = wm_state.screen->width_in_pixels;
      new_rect.y() = wm_state.screen->height_in_pixels;

      ConfigureClient(wm_state.conn, client_window, client, new_rect, false);
      client.rect = Rect{};
    }
  };

  for (size_t istack = 0; istack < wm_state.stacks.size(); ++istack) {
    if (istack == wm_state.active_stack_idx) continue;

    for (xcb_window_t client_window : wm_state.stacks[istack].windows) {
      Client& client = wm_state.clients.at(client_window);
      hide(client_window, client);
      for (xcb_window_t subwindow : client.subwindows)
        hide(subwindow, wm_state.clients.at(subwindow));
    }
  }
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
}

std::string GetActiveClientBarText(WMState& wm_state) {
  xcb_window_t active_window = GetActiveWindow(wm_state);
  if (!active_window || !wm_state.clients.contains(active_window))
    return "nyla: no active client";

  return wm_state.clients.at(active_window).name;
}

}  // namespace nyla
