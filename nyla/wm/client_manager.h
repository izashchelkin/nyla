#pragma once

#include <cstddef>
#include <cstdint>

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

  std::vector<xcb_window_t> windows;
  xcb_window_t active_window;
};

struct Client {
  Rect rect;
  std::string name;

  bool wm_hints_input;
  bool wm_take_focus;
  bool wm_delete_window;

  uint32_t max_width;
  uint32_t max_height;

  bool wants_configure_notify;

  xcb_window_t transient_for;
  std::vector<xcb_window_t> subwindows;
};

struct WMState {
  xcb_connection_t* conn;
  xcb_screen_t* screen;
  Atoms* atoms;

  bool layout_dirty;
  bool follow;

  absl::flat_hash_map<xcb_window_t, Client> clients;

  std::vector<ClientStack> stacks;
  size_t active_stack_idx;
};

inline ClientStack& GetActiveStack(WMState& wm_state) {
  CHECK_LT(wm_state.active_stack_idx, wm_state.stacks.size());
  return wm_state.stacks.at(wm_state.active_stack_idx);
}

std::string GetActiveClientBarText(WMState& wm_state);
void CheckFocusTheft(WMState& wm_state);

void ManageClientsStartup(WMState& wm_state);
void ManageClient(WMState& wm_state, xcb_window_t client_window, bool focus);
void UnmanageClient(WMState& wm_state, xcb_window_t window);

void CloseActive(WMState& wm_state);

void ToggleZoom(WMState& wm_state);
void ToggleFollow(WMState& wm_state);

void ApplyLayoutChanges(WMState& wm_state, const Rect& screen_rect,
                        uint32_t padding);

void MoveLocalNext(WMState& wm_state, xcb_timestamp_t time);
void MoveLocalPrev(WMState& wm_state, xcb_timestamp_t time);

void MoveStackNext(WMState& wm_state, xcb_timestamp_t time);
void MoveStackPrev(WMState& wm_state, xcb_timestamp_t time);

void NextLayout(WMState& wm_state);

}  // namespace nyla
