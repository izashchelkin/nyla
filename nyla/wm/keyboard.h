#pragma once

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <functional>
#include <span>

namespace nyla {

class Keybind {
 public:
  Keybind(const char* keyname,
          const std::function<void(xcb_timestamp_t)>& action)
      : keyname_{keyname}, action_{action} {}

  const char* keyname() const { return keyname_; }
  xcb_keycode_t& keycode() { return keycode_; }
  const xcb_keycode_t& keycode() const { return keycode_; }

  void RunAction(xcb_timestamp_t time) const { return action_(time); }
  bool Resolve(xkb_keymap* keymap);

 private:
  const char* keyname_;
  std::function<void(xcb_timestamp_t)> action_;
  xcb_keycode_t keycode_;
};

bool InitKeyboard(xcb_connection_t* conn);
bool BindKeyboard(xcb_connection_t* conn, xcb_window_t root_window,
                  uint16_t modifier, std::span<Keybind> keybinds);
bool HandleKeyPress(xcb_keycode_t keycode, std::span<const Keybind> keybinds,
                    xcb_timestamp_t time);

}  // namespace nyla
