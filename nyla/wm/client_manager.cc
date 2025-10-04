#include "nyla/wm/client_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
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
  for (auto& stack : wm_state.stacks) stack.zoom = false;
}

static void SetNormalBorder(WMState& wm_state, xcb_window_t window) {
  if (window) {
    xcb_change_window_attributes(wm_state.conn, window, XCB_CW_BORDER_PIXEL,
                                 (uint32_t[]){wm_state.screen.black_pixel});
  }
}

static void SetActiveBorder(WMState& wm_state, xcb_window_t window) {
  if (window) {
    xcb_change_window_attributes(wm_state.conn, window, XCB_CW_BORDER_PIXEL,
                                 (uint32_t[]){wm_state.screen.white_pixel});
  }
}

static bool UpdateClientProperties(WMState& wm_state,
                                   xcb_window_t client_window, Client& client) {
  auto wm_hints = FetchPropertyStruct<WM_Hints>(
      wm_state.conn, client_window, XCB_ATOM_WM_HINTS, XCB_ATOM_WM_HINTS);
  if (!wm_hints) return false;

  auto wm_protocols = FetchPropertyVector<xcb_atom_t>(
      wm_state.conn, client_window, wm_state.atoms.wm_protocols, XCB_ATOM_ATOM);
  if (!wm_protocols) return false;

  client.input = wm_hints->input;

  client.wm_delete_window = false;
  client.wm_take_focus = false;

  for (xcb_atom_t atom : *wm_protocols) {
    if (atom == wm_state.atoms.wm_delete_window) client.wm_delete_window = true;
    if (atom == wm_state.atoms.wm_take_focus) client.wm_take_focus = true;
  }

  auto name = FetchPropertyVector<char>(wm_state.conn, client_window,
                                        wm_state.atoms.wm_name, XCB_ATOM_ANY);
  if (!name) return false;

  client.name = std::string_view{name->data(), name->size()};

  return true;
}

static void SetInputFocus(WMState& wm_state, xcb_window_t window,
                          xcb_timestamp_t time) {
  if (!window || !wm_state.clients.contains(window)) {
    xcb_set_input_focus(wm_state.conn, XCB_INPUT_FOCUS_POINTER_ROOT,
                        wm_state.screen.root, time);
    return;
  }

  const Client& client = wm_state.clients.at(window);

  xcb_set_input_focus(wm_state.conn, XCB_INPUT_FOCUS_POINTER_ROOT,
                      client.input ? window : wm_state.screen.root, time);

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

void ManageClientsStartup(WMState& wm_state) {
  xcb_query_tree_reply_t* tree_reply = xcb_query_tree_reply(
      wm_state.conn, xcb_query_tree(wm_state.conn, wm_state.screen.root),
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

    ManageClient(wm_state, client_window, false);
  }

  free(tree_reply);
}

void ManageClient(WMState& wm_state, xcb_window_t client_window, bool focus) {
  xcb_change_window_attributes(wm_state.conn, client_window, XCB_CW_EVENT_MASK,
                               (uint32_t[]){XCB_EVENT_MASK_ENTER_WINDOW,
                                            XCB_EVENT_MASK_PROPERTY_CHANGE});

  if (auto [it, inserted] =
          wm_state.clients.try_emplace(client_window, Client{});
      inserted) {
    auto& [client_window, client] = *it;

    ClientStack& stack = GetActiveStack(wm_state);

    if (!UpdateClientProperties(wm_state, client_window, client)) {
      LOG(WARNING) << "window gonsky?";
      return;
    }

    ClearZoom(wm_state);
    stack.client_windows.emplace_back(client_window);

    if (focus) {
      xcb_window_t active_window = GetActiveWindow(wm_state);

      if (active_window != client_window) {
        SetNormalBorder(wm_state, active_window);

        stack.active_client_window = client_window;
        SetInputFocus(wm_state, client_window, XCB_CURRENT_TIME);
        SetActiveBorder(wm_state, client_window);
      }
    }
  }
}

void UnmanageClient(WMState& wm_state, xcb_window_t window) {
  wm_state.clients.erase(window);

  for (size_t istack = 0; istack < wm_state.stacks.size(); ++istack) {
    ClientStack& stack = wm_state.stacks[istack];

    auto it =
        std::find_if(stack.client_windows.begin(), stack.client_windows.end(),
                     [window](xcb_window_t elem) { return elem == window; });
    if (it == stack.client_windows.end()) continue;
    stack.client_windows.erase(it);

    if (stack.active_client_window == window) {
      stack.active_client_window = XCB_WINDOW_NONE;

      if (istack == wm_state.active_stack_idx) {
        if (stack.client_windows.empty()) {
          xcb_set_input_focus(wm_state.conn, XCB_INPUT_FOCUS_POINTER_ROOT,
                              wm_state.screen.root, XCB_CURRENT_TIME);
        } else {
          stack.active_client_window = stack.client_windows[0];
          SetInputFocus(wm_state, stack.active_client_window, XCB_CURRENT_TIME);
          SetActiveBorder(wm_state, stack.active_client_window);
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
  std::vector<Rect> layout = ComputeLayout(
      screen_rect, stack.client_windows.size(), padding, stack.layout_type);
  CHECK_EQ(layout.size(), stack.client_windows.size());

  for (auto [layout_rect, client_window] :
       std::ranges::views::zip(layout, stack.client_windows)) {
    Client& client = wm_state.clients.at(client_window);

    Rect rect = (client_window == stack.active_client_window && stack.zoom)
                    ? ApplyPadding(screen_rect, padding)
                    : layout_rect;
    if (rect != client.rect) {
      ConfigureClient(wm_state.conn, client_window, client, rect, true);
      client.rect = rect;
    }
  }

  for (size_t istack = 0; istack < wm_state.stacks.size(); ++istack) {
    if (istack == wm_state.active_stack_idx) continue;

    for (xcb_window_t client_window : wm_state.stacks[istack].client_windows) {
      Client& client = wm_state.clients.at(client_window);
      if (client.rect != Rect{}) {
        Rect new_rect = client.rect;
        new_rect.x() = wm_state.screen.width_in_pixels;
        new_rect.y() = wm_state.screen.height_in_pixels;

        ConfigureClient(wm_state.conn, client_window, client, new_rect, false);
        client.rect = Rect{};
      }
    }
  }
}

static void ChangeStack(WMState& wm_state, xcb_timestamp_t time,
                        auto compute_idx) {
  ClearZoom(wm_state);
  SetNormalBorder(wm_state, GetActiveWindow(wm_state));

  wm_state.active_stack_idx =
      compute_idx(wm_state.active_stack_idx + wm_state.stacks.size()) %
      wm_state.stacks.size();

  xcb_window_t window = GetActiveWindow(wm_state);
  SetActiveBorder(wm_state, window);
  if (!window) window = wm_state.screen.root;
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
  if (stack.client_windows.empty()) return;

  if (stack.active_client_window) {
    // TODO: DEBUG THIS!!!

    if (stack.client_windows.size() < 2) return;

    SetNormalBorder(wm_state, stack.active_client_window);

    size_t iactive = 0;
    for (; iactive < stack.client_windows.size(); ++iactive) {
      if (stack.client_windows[iactive] == stack.active_client_window) break;
    }
    CHECK_LT(iactive, stack.client_windows.size());

    iactive = compute_idx(iactive + stack.client_windows.size()) %
              stack.client_windows.size();

    stack.active_client_window = stack.client_windows[iactive];
  } else {
    stack.active_client_window = stack.client_windows[0];
  }

  SetInputFocus(wm_state, stack.active_client_window, time);
  SetActiveBorder(wm_state, stack.active_client_window);
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

}  // namespace nyla
