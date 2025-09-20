#include "nyla/managers/keyboard.h"

#include <xcb/xproto.h>

#include <algorithm>
#include <cstdint>
#include <span>

#include "absl/cleanup/cleanup.h"
#include "xcb/xcb.h"
#include "xkbcommon/xkbcommon-x11.h"
#include "xkbcommon/xkbcommon.h"

namespace nyla {

bool InitKeyboard(xcb_connection_t* conn) {
  uint16_t major_xkb_version_out, minor_xkb_version_out;
  uint8_t base_event_out, base_error_out;

  constexpr uint16_t kXkbMinMajorVersion = XKB_X11_MIN_MAJOR_XKB_VERSION;
  constexpr uint16_t kXkbMinMinorVersion = XKB_X11_MIN_MINOR_XKB_VERSION;
  if (!xkb_x11_setup_xkb_extension(
          conn, kXkbMinMajorVersion, kXkbMinMinorVersion,
          XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, &major_xkb_version_out,
          &minor_xkb_version_out, &base_event_out, &base_error_out)) {
    return false;
  }

  return major_xkb_version_out > kXkbMinMajorVersion ||
         (major_xkb_version_out == kXkbMinMajorVersion &&
          minor_xkb_version_out >= kXkbMinMinorVersion);
}

bool Keybind::Resolve(xkb_keymap* keymap) {
  xcb_keycode_t keycode = xkb_keymap_key_by_name(keymap, keyname_);
  if (xkb_keycode_is_legal_x11(keycode)) {
    keycode_ = keycode;
    return true;
  } else {
    return false;
  }
}

bool BindKeyboard(xcb_connection_t* conn, xcb_window_t root_window,
                  uint16_t modifier, std::span<Keybind> keybinds) {
  if (!modifier) return false;

  xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!ctx) return false;
  absl::Cleanup ctx_freer = [ctx]() { xkb_context_unref(ctx); };

  int32_t device_id = xkb_x11_get_core_keyboard_device_id(conn);
  if (device_id == -1) return false;

  xkb_keymap* keymap = xkb_x11_keymap_new_from_device(
      ctx, conn, device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!keymap) return false;
  absl::Cleanup keymap_freer = [keymap]() { xkb_keymap_unref(keymap); };

  xcb_grab_server(conn);
  absl::Cleanup server_ungrab = [conn]() { xcb_ungrab_server(conn); };

  xcb_ungrab_key(conn, XCB_GRAB_ANY, root_window, XCB_MOD_MASK_ANY);

  for (Keybind& keybind : keybinds) {
    if (!keybind.Resolve(keymap)) return false;

    xcb_grab_key(conn, 0, root_window, modifier, keybind.keycode(),
                 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    // TODO: locks
  }

  xcb_flush(conn);
  return true;
}

bool HandleKeyPress(xcb_keycode_t keycode, std::span<const Keybind> keybinds) {
  auto it = std::find_if(
      keybinds.begin(), keybinds.end(),
      [keycode](const auto& keybind) { return keybind.keycode() == keycode; });
  if (it != keybinds.end()) {
    it->RunAction();
    return true;
  }
  return false;
}

}  // namespace nyla
