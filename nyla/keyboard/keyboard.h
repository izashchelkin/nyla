#ifndef NYLA_X11_KEYBOARD_H
#define NYLA_X11_KEYBOARD_H

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <functional>
#include <span>

namespace nyla {

class Keybind {
 public:
  Keybind(const char* keyname, const std::function<void(void)>& action)
      : keyname_{keyname}, action_{action} {}

  const char* keyname() const { return keyname_; }
  xcb_keycode_t& keycode() { return keycode_; }
  const xcb_keycode_t& keycode() const { return keycode_; }

  void RunAction() const { return action_(); }
  bool Resolve(xkb_keymap* keymap);

 private:
  const char* keyname_;
  std::function<void(void)> action_;
  xcb_keycode_t keycode_;
};

bool InitKeyboard(xcb_connection_t* conn);
bool BindKeyboard(xcb_connection_t* conn, xcb_window_t root_window,
                  uint16_t modifier, std::span<Keybind> keybinds);
bool HandleKeyPress(xcb_keycode_t keycode, std::span<const Keybind> keybinds);

}  // namespace nyla

#endif
