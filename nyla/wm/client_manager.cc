#include "nyla/wm/client_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "nyla/layout/layout.h"
#include "nyla/protocols/atoms.h"
#include "nyla/protocols/properties.h"
#include "nyla/protocols/wm_hints.h"
#include "nyla/protocols/wm_protocols.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

void SetInputFocus(WMState& wm_state, xcb_window_t window) {
  if (window)
    Send_WM_Take_Focus(wm_state.conn, window, wm_state.atoms, XCB_CURRENT_TIME);
  else
    window = wm_state.screen.root;

  xcb_set_input_focus(wm_state.conn, XCB_NONE, window, XCB_CURRENT_TIME);
}

void ManageClient(WMState& wm_state, xcb_window_t window) {
  xcb_change_window_attributes(wm_state.conn, window, XCB_CW_EVENT_MASK,
                               (uint32_t[]){XCB_EVENT_MASK_ENTER_WINDOW,
                                            XCB_EVENT_MASK_PROPERTY_CHANGE});

  if (auto res = wm_state.clients.try_emplace(window, Client{}); res.second) {
    CHECK_LT(wm_state.active_stack_idx, wm_state.stacks.size());
    ClientStack& stack = wm_state.stacks[wm_state.active_stack_idx];
    stack.client_windows.emplace_back(window);

    Client& client = res.first->second;

    client.input =
        FetchPropertyStruct<WM_Hints>(wm_state.conn, window, XCB_ATOM_WM_HINTS,
                                      XCB_ATOM_WM_HINTS)
            .input;

    for (const xcb_atom_t& protocol_atom : FetchPropertyList<xcb_atom_t>(
             wm_state.conn, window, wm_state.atoms.wm_protocols,
             XCB_ATOM_ATOM)) {
      if (protocol_atom == wm_state.atoms.wm_delete_window) {
        client.wm_delete_window = true;
        continue;
      }
      if (protocol_atom == wm_state.atoms.wm_take_focus) {
        client.wm_take_focus = true;
        continue;
      }
    }
  }
}

void UnmanageClient(WMState& wm_state, xcb_window_t window) {
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
      }
    }
    return;
  }
}

void ApplyLayoutChanges(WMState& wm_state, const std::vector<Rect>& layout) {
  ClientStack& stack = wm_state.stacks[wm_state.active_stack_idx];
  CHECK_EQ(layout.size(), stack.client_windows.size());

  for (const auto& [rect, client_window] :
       std::ranges::views::zip(layout, stack.client_windows)) {
    Client& client = wm_state.clients.at(client_window);

    if (rect != client.rect) {
      client.rect = rect;

      xcb_configure_window(
          wm_state.conn, client_window,
          XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
              XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH,
          (uint32_t[]){static_cast<uint32_t>(rect.x()),
                       static_cast<uint32_t>(rect.y()), rect.width(),
                       rect.height(), 2});
    }
  }

  for (size_t istack = 0; istack < wm_state.stacks.size(); ++istack) {
    if (istack == wm_state.active_stack_idx) continue;

    for (xcb_window_t client_window : wm_state.stacks[istack].client_windows) {
      Client& client = wm_state.clients.at(client_window);
      if (client.rect != Rect{}) {
        client.rect = Rect{};
        xcb_configure_window(wm_state.conn, client_window,
                             XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                             (uint32_t[]){wm_state.screen.width_in_pixels,
                                          wm_state.screen.height_in_pixels});
      }
    }
  }
}

void NextStack(WMState& wm_state) {
  CHECK_LT(wm_state.active_stack_idx, wm_state.stacks.size());
  if (wm_state.stacks[wm_state.active_stack_idx].active_client_window) {
    xcb_change_window_attributes(
        wm_state.conn,
        wm_state.stacks[wm_state.active_stack_idx].active_client_window,
        XCB_CW_BORDER_PIXEL, (uint32_t[]){wm_state.screen.black_pixel});
  }

  wm_state.active_stack_idx =
      (wm_state.active_stack_idx + 1) % wm_state.stacks.size();

  xcb_window_t window =
      wm_state.stacks[wm_state.active_stack_idx].active_client_window;
  if (!window) window = wm_state.screen.root;

  SetInputFocus(wm_state, window);
  xcb_change_window_attributes(wm_state.conn, window, XCB_CW_BORDER_PIXEL,
                               (uint32_t[]){wm_state.screen.white_pixel});
}

void PrevStack(WMState& wm_state) {
  CHECK_LT(wm_state.active_stack_idx, wm_state.stacks.size());
  if (wm_state.stacks[wm_state.active_stack_idx].active_client_window) {
    xcb_change_window_attributes(
        wm_state.conn,
        wm_state.stacks[wm_state.active_stack_idx].active_client_window,
        XCB_CW_BORDER_PIXEL, (uint32_t[]){wm_state.screen.black_pixel});
  }

  wm_state.active_stack_idx =
      (wm_state.active_stack_idx + wm_state.stacks.size() - 1) %
      wm_state.stacks.size();

  xcb_window_t window =
      wm_state.stacks[wm_state.active_stack_idx].active_client_window;
  if (!window) window = wm_state.screen.root;

  SetInputFocus(wm_state, window);
  xcb_change_window_attributes(wm_state.conn, window, XCB_CW_BORDER_PIXEL,
                               (uint32_t[]){wm_state.screen.white_pixel});
}

void NextLayout(WMState& wm_state) {
  CHECK_LT(wm_state.active_stack_idx, wm_state.stacks.size());
  CycleLayoutType(wm_state.stacks[wm_state.active_stack_idx].layout_type);
}

void MoveClientFocus(WMState& wm_state, ssize_t idelta) {
  CHECK_LT(wm_state.active_stack_idx, wm_state.stacks.size());
  CHECK_LT(static_cast<size_t>(std::abs(idelta)), wm_state.stacks.size());

  ClientStack& stack = wm_state.stacks[wm_state.active_stack_idx];
  if (stack.client_windows.empty()) return;

  if (stack.active_client_window) {
		// TODO: DEBUG THIS!!!

    if (stack.client_windows.size() < 2) return;

    xcb_change_window_attributes(wm_state.conn, stack.active_client_window,
                                 XCB_CW_BORDER_PIXEL,
                                 (uint32_t[]){wm_state.screen.black_pixel});

    size_t iactive = 0;
    for (; iactive < stack.client_windows.size(); ++iactive) {
      if (stack.client_windows[iactive] == stack.active_client_window) break;
    }
    CHECK_LT(iactive, stack.client_windows.size());

    iactive = (iactive + stack.client_windows.size() + idelta) %
              stack.client_windows.size();

    stack.active_client_window = stack.client_windows[iactive];
  } else {
    stack.active_client_window = stack.client_windows[0];
  }

  SetInputFocus(wm_state, stack.active_client_window);
  xcb_change_window_attributes(wm_state.conn, stack.active_client_window,
                               XCB_CW_BORDER_PIXEL,
                               (uint32_t[]){wm_state.screen.white_pixel});
}

void FocusWindow(xcb_connection_t* conn, const xcb_screen_t& screen,
                 xcb_window_t window) {
  // xcb_window_t old_focused_window =
  //     stack().clients[stack().focused.index].window;
  // if (old_focused_window == window) return;
  //
  // for (size_t i = 0; i < stack().clients.size(); ++i) {
  //   auto& client = stack().clients[i];
  //   if (client.window == window) {
  //     xcb_change_window_attributes(conn, old_focused_window,
  //                                  XCB_CW_BORDER_PIXEL,
  //                                  (uint32_t[]){screen.black_pixel});
  //
  //     stack().focused.index = i;
  //     stack().focused.uid = client.guid;
  //
  //     xcb_set_input_focus(conn, XCB_INPUT_FOCUS_PARENT, client.window,
  //                         XCB_CURRENT_TIME);
  //     xcb_change_window_attributes(conn, client.window, XCB_CW_BORDER_PIXEL,
  //                                  (uint32_t[]){screen.white_pixel});
  //     return;
  //   }
  // }
}

xcb_window_t GetFocusedWindow() {
  return 0;

  // if (!stack().focused.uid) return 0;
  //
  // CHECK_LT(stack().focused.index, stack().clients.size());
  // return stack().clients[stack().focused.index].window;
}

}  // namespace nyla
