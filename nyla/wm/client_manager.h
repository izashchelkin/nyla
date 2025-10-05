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

struct ClientStack {
  LayoutType layout_type;
  bool zoom;
  bool follow;

  std::vector<xcb_window_t> windows;
  xcb_window_t active_window;
};

struct Client {
  Rect rect;
  std::string name;

  bool input;
  bool wm_take_focus;
  bool wm_delete_window;
  bool wants_configure_notify;

  xcb_window_t transient_for;
  std::vector<xcb_window_t> subwindows;
  xcb_window_t active_subwindow;
};

struct WMState {
  xcb_connection_t* conn;
  xcb_screen_t* screen;
  Atoms* atoms;

  absl::flat_hash_map<xcb_window_t, Client> clients;

  std::vector<ClientStack> stacks;
  size_t active_stack_idx;
};

inline ClientStack& GetActiveStack(WMState& wm_state) {
  CHECK_LT(wm_state.active_stack_idx, wm_state.stacks.size());
  return wm_state.stacks.at(wm_state.active_stack_idx);
}

inline xcb_window_t GetActiveWindow(WMState& wm_state) {
  const ClientStack& stack = GetActiveStack(wm_state);
  if (!stack.active_window) return 0;

  const Client& client = wm_state.clients.at(stack.active_window);
  CHECK(!client.transient_for);

  if (!client.active_subwindow) return stack.active_window;

  CHECK(wm_state.clients.contains(client.active_subwindow));
  return client.active_subwindow;
}

std::string GetActiveClientBarText(WMState& wm_state);

void ManageClientsStartup(WMState& wm_state);
void ManageClient(WMState& wm_state, xcb_window_t client_window, bool focus);
void UnmanageClient(WMState& wm_state, xcb_window_t window);

void ApplyLayoutChanges(WMState& wm_state, const Rect& screen_rect,
                        uint32_t padding);

void NextFocus(WMState& wm_state, xcb_timestamp_t time);
void PrevFocus(WMState& wm_state, xcb_timestamp_t time);

void NextStack(WMState& wm_state, xcb_timestamp_t time);
void PrevStack(WMState& wm_state, xcb_timestamp_t time);

void NextLayout(WMState& wm_state);

}  // namespace nyla
