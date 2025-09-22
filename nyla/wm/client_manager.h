#pragma once

#include <cstddef>

#include "absl/container/flat_hash_map.h"
#include "nyla/commons/rect.h"
#include "nyla/layout/layout.h"
#include "nyla/protocols/atoms.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

struct Client {
  Rect rect;
  bool input;
  bool wm_take_focus;
  bool wm_delete_window;
};

struct ClientStack {
  std::vector<xcb_window_t> client_windows;
  xcb_window_t active_client_window;

  LayoutType layout_type;
};

struct WMState {
  xcb_connection_t* conn;
  xcb_screen_t screen;
  Atoms atoms;
  absl::flat_hash_map<xcb_window_t, Client> clients;
  std::vector<ClientStack> stacks;
  size_t active_stack_idx;
};

void ManageClient(WMState& wm_state, xcb_window_t window);
void UnmanageClient(WMState& wm_state, xcb_window_t window);
void ApplyLayoutChanges(WMState& wm_state, const std::vector<Rect>& layout);
void NextStack(WMState& wm_state);
void PrevStack(WMState& wm_state);
void NextLayout(WMState& wm_state);
void MoveClientFocus(WMState& wm_state, ssize_t idelta);
void SetInputFocus(WMState& wm_state, xcb_window_t window);

}  // namespace nyla
