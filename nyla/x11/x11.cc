#include "nyla/x11/x11.h"

#include <string_view>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xkbcommon/xkbcommon-x11.h"
#include "xkbcommon/xkbcommon.h"

namespace nyla {

X11_State x11;

void X11_Initialize() {
  int iscreen;
  x11.conn = xcb_connect(nullptr, &iscreen);
  if (xcb_connection_has_error(x11.conn))
    LOG(QFATAL) << "could not connect to X server";

  x11.screen = xcb_aux_get_screen(x11.conn, iscreen);
  CHECK(x11.screen);

#define X(atom) \
  x11.atoms.atom = X11_InternAtom(x11.conn, absl::AsciiStrToUpper(#atom));
  Nyla_X11_Atoms(X)
#undef X

  {
    uint16_t major_xkb_version_out, minor_xkb_version_out;
    uint8_t base_event_out, base_error_out;

    if (!xkb_x11_setup_xkb_extension(
            x11.conn, XKB_X11_MIN_MAJOR_XKB_VERSION,
            XKB_X11_MIN_MINOR_XKB_VERSION, XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
            &major_xkb_version_out, &minor_xkb_version_out, &base_event_out,
            &base_error_out)) {
      LOG(QFATAL) << "could not setup xkb extension";
    }

    if (major_xkb_version_out < XKB_X11_MIN_MAJOR_XKB_VERSION ||
        (major_xkb_version_out == XKB_X11_MIN_MAJOR_XKB_VERSION &&
         minor_xkb_version_out < XKB_X11_MIN_MINOR_XKB_VERSION)) {
      LOG(QFATAL) << "could not setup xkb extension";
    }
  }
}

xcb_atom_t X11_InternAtom(xcb_connection_t* conn, std::string_view name,

                          bool only_if_exists) {
  xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
      conn, xcb_intern_atom(conn, only_if_exists, name.size(), name.data()),
      nullptr);
  absl::Cleanup reply_freer = [reply] {
    if (reply) free(reply);
  };

  if (!reply || !reply->atom) {
    LOG(FATAL) << "could not intern atom " << name;
  }

  return reply->atom;
}

void X11_SendClientMessage32(xcb_window_t window, xcb_atom_t type,
                             xcb_atom_t arg1, uint32_t arg2, uint32_t arg3,
                             uint32_t arg4) {
  xcb_client_message_event_t event = {
      .response_type = XCB_CLIENT_MESSAGE,
      .format = 32,
      .window = window,
      .type = type,
      .data = {.data32 = {arg1, arg2, arg3, arg4}},
  };

  xcb_send_event(x11.conn, false, window, XCB_EVENT_MASK_NO_EVENT,
                 reinterpret_cast<const char*>(&event));
}

void X11_Send_WM_Take_Focus(xcb_window_t window, uint32_t time) {
  X11_SendClientMessage32(window, x11.atoms.wm_protocols,
                          x11.atoms.wm_take_focus, time, 0, 0);
}

void X11_Send_WM_Delete_Window(xcb_window_t window) {
  X11_SendClientMessage32(window, x11.atoms.wm_protocols,
                          x11.atoms.wm_delete_window, XCB_CURRENT_TIME, 0, 0);
}

void X11_SendConfigureNotify(xcb_window_t window, xcb_window_t parent,
                             int16_t x, int16_t y, uint16_t width,
                             uint16_t height, uint16_t border_width) {
  xcb_configure_notify_event_t event = {
      .response_type = XCB_CONFIGURE_NOTIFY,
      .window = window,
      .x = x,
      .y = y,
      .width = width,
      .height = height,
      .border_width = border_width,
  };

  xcb_send_event(x11.conn, false, window, XCB_EVENT_MASK_STRUCTURE_NOTIFY,
                 reinterpret_cast<const char*>(&event));
}

//

bool X11_InitializeKeyResolver(X11_KeyResolver& key_resolver) {
  key_resolver.ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!key_resolver.ctx) return false;

  const int32_t device_id = xkb_x11_get_core_keyboard_device_id(x11.conn);
  if (device_id == -1) return false;

  key_resolver.keymap = xkb_x11_keymap_new_from_device(
      key_resolver.ctx, x11.conn, device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!key_resolver.keymap) return false;

  return true;
}

void X11_FreeKeyResolver(X11_KeyResolver& key_resolver) {
  xkb_keymap_unref(key_resolver.keymap);
  xkb_context_unref(key_resolver.ctx);
}

xcb_keycode_t X11_ResolveKeyCode(const X11_KeyResolver& key_resolver,
                                 std::string_view keyname) {
  const xkb_keycode_t keycode =
      xkb_keymap_key_by_name(key_resolver.keymap, keyname.data());
  CHECK(xkb_keycode_is_legal_x11(keycode));
  return keycode;
}

}  // namespace nyla
