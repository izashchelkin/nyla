#pragma once

#include <cstddef>
#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "nyla/commons/rect.h"
#include "nyla/layout/layout.h"
#include "nyla/protocols/atoms.h"
#include "nyla/wm/keyboard.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

struct WindowStack {
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

  absl::flat_hash_map<xcb_atom_t, xcb_get_property_cookie_t> property_cookies;
};

template <typename Sink>
void AbslStringify(Sink& sink, const Client& c) {
  absl::Format(&sink, "Window{ rect=%v, input=%v, take_focus=%v }", c.rect,
               c.wm_hints_input, c.wm_take_focus);
}

template <typename Key, typename Val>
using Map = absl::flat_hash_map<Key, Val>;

extern xcb_connection_t* wm_conn;
extern xcb_screen_t wm_screen;
extern X11Atoms atoms;

extern uint32_t wm_bar_height;
extern bool wm_bar_dirty;
extern bool wm_layout_dirty;
extern bool wm_follow;

extern Map<xcb_window_t, Client> wm_clients;
extern std::vector<xcb_window_t> wm_pending_clients;

using WMPropertyChangeHandler = void (*)(xcb_window_t, Client&,
                                         xcb_get_property_reply_t*);
extern Map<xcb_atom_t, WMPropertyChangeHandler> wm_property_change_handlers;

extern std::vector<WindowStack> wm_stacks;
extern size_t wm_active_stack_idx;

//

inline WindowStack& GetActiveStack() {
  CHECK_LT(wm_active_stack_idx, wm_stacks.size());
  return wm_stacks.at(wm_active_stack_idx);
}

void InitializeWM();
void ProcessWMEvents(const bool& is_running, uint16_t modifier,
                     std::span<Keybind> keybinds);
void ProcessWM();

std::string GetActiveClientBarText();

void ManageClientsStartup();
void ManageClient(xcb_window_t client_window);
void UnmanageClient(xcb_window_t window);

void CloseActive();

void ToggleZoom();
void ToggleFollow();

void ApplyLayoutChanges(const Rect& screen_rect, uint32_t padding);

void MoveLocalNext(xcb_timestamp_t time);
void MoveLocalPrev(xcb_timestamp_t time);

void MoveStackNext(xcb_timestamp_t time);
void MoveStackPrev(xcb_timestamp_t time);

void NextLayout();

}  // namespace nyla
