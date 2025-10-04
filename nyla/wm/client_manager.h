#pragma once

#include <cstddef>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "nyla/commons/rect.h"
#include "nyla/layout/layout.h"
#include "nyla/protocols/atoms.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

struct Client {
  Rect rect;
  std::string name;
  bool input;
  bool wm_take_focus;
  bool wm_delete_window;
  bool wants_configure_notify;
};

struct ClientStack {
  std::vector<xcb_window_t> client_windows;
  xcb_window_t active_client_window;
  LayoutType layout_type;
  bool zoom;
  bool follow;
};

struct WMState {
  xcb_connection_t* conn;
  xcb_screen_t screen;
  Atoms atoms;
  absl::flat_hash_map<xcb_window_t, Client> clients;
  std::vector<ClientStack> stacks;
  size_t active_stack_idx;
};

inline ClientStack& GetActiveStack(WMState& wm_state) {
  CHECK_LT(wm_state.active_stack_idx, wm_state.stacks.size());
  return wm_state.stacks[wm_state.active_stack_idx];
}

inline xcb_window_t GetActiveWindow(WMState& wm_state) {
  return GetActiveStack(wm_state).active_client_window;
}

inline decltype(WMState::clients)::iterator GetActiveClient(WMState& wm_state) {
  xcb_window_t window = GetActiveWindow(wm_state);
  if (!window) return wm_state.clients.end();
  return wm_state.clients.find(window);
}

void ManageClientsStartup(WMState& wm_state);
void ManageClient(WMState& wm_state, xcb_window_t window);
void UnmanageClient(WMState& wm_state, xcb_window_t window);
void ApplyLayoutChanges(WMState& wm_state, const Rect& screen_rect,
                        uint32_t padding);
void NextStack(WMState& wm_state, xcb_timestamp_t time);
void PrevStack(WMState& wm_state, xcb_timestamp_t time);
void NextLayout(WMState& wm_state);
void MoveClientFocus(WMState& wm_state, ssize_t idelta, xcb_timestamp_t time);
void SetInputFocus(WMState& wm_state, xcb_window_t window,
                   xcb_timestamp_t time);

void BorderActive(WMState& wm_state, xcb_window_t window);
void BorderNormal(WMState& wm_state, xcb_window_t window);

}  // namespace nyla
