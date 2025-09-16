#ifndef NYLA_X11_KEYBOARD_H
#define NYLA_X11_KEYBOARD_H

#include <xcb/xcb.h>
#include <xkbcommon/xkbcommon.h>

#include <functional>
#include <span>

namespace nyla {

bool InitKeyboard(xcb_connection_t* conn);

struct KeyBind {
  const char* keyname;
  std::function<void(void)> action;
  xcb_keycode_t keycode;
};

bool BindKeyboard(xcb_connection_t* conn, xcb_window_t root_window,
                  xcb_mod_mask_t modifier, std::span<KeyBind> keybinds);

bool HandleKeyPress(xcb_keycode_t keycode, std::span<const KeyBind> keybinds);

}  // namespace nyla

#endif
