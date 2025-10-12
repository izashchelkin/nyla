#pragma once

#include <cstdint>

#include "absl/container/flat_hash_map.h"
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

  bool urgent;
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

extern xcb_connection_t* wm_conn;
extern xcb_screen_t wm_screen;
extern X11Atoms atoms;

extern bool wm_bar_dirty;

//

void InitializeWM();
void ProcessWMEvents(const bool& is_running, uint16_t modifier,
                     std::span<Keybind> keybinds);
void ProcessWM();
void UpdateBar();

std::string DumpClients();
std::string GetActiveClientBarText();

void ManageClientsStartup();

void CloseActive();

void ToggleZoom();
void ToggleFollow();

void MoveLocalNext(xcb_timestamp_t time);
void MoveLocalPrev(xcb_timestamp_t time);

void MoveStackNext(xcb_timestamp_t time);
void MoveStackPrev(xcb_timestamp_t time);

void NextLayout();

}  // namespace nyla
