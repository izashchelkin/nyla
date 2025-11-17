#include "nyla/x11/x11.h"

#include <cstdint>
#include <string_view>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "nyla/commons/memory/optional.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xcb/xinput.h"
#include "xkbcommon/xkbcommon-x11.h"
#include "xkbcommon/xkbcommon.h"

// Source - https://stackoverflow.com/a/72177598
// Posted by HolyBlackCat, modified by community. See post 'Timeline' for change history
// Retrieved 2025-11-17, License - CC BY-SA 4.0

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define explicit explicit_
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <xcb/xkb.h>

#undef explicit

namespace nyla {

X11_State x11;

void X11_Initialize() {
  int iscreen;
  x11.conn = xcb_connect(nullptr, &iscreen);
  if (xcb_connection_has_error(x11.conn)) LOG(QFATAL) << "could not connect to X server";

  x11.screen = xcb_aux_get_screen(x11.conn, iscreen);
  CHECK(x11.screen);

#define X(atom) x11.atoms.atom = X11_InternAtom(x11.conn, absl::AsciiStrToUpper(#atom));
  Nyla_X11_Atoms(X)
#undef X

  {
    uint16_t major_xkb_version_out, minor_xkb_version_out;
    uint8_t base_event_out, base_error_out;

    if (!xkb_x11_setup_xkb_extension(x11.conn, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION,
                                     XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, &major_xkb_version_out,
                                     &minor_xkb_version_out, &base_event_out, &base_error_out)) {
      LOG(QFATAL) << "could not set up xkb extension";
    }

    if (major_xkb_version_out < XKB_X11_MIN_MAJOR_XKB_VERSION ||
        (major_xkb_version_out == XKB_X11_MIN_MAJOR_XKB_VERSION &&
         minor_xkb_version_out < XKB_X11_MIN_MINOR_XKB_VERSION)) {
      LOG(QFATAL) << "could not set up xkb extension";
    }

    xcb_generic_error_t* err = nullptr;
    if (!Unown(xcb_xkb_per_client_flags_reply(
            x11.conn,
            xcb_xkb_per_client_flags(x11.conn, XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
                                     XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 0, 0, 0),
            &err)) ||
        err) {
      LOG(QFATAL) << "could not set up detectable autorepeat";
    }
  }

  {
    x11.ext_xi2 = xcb_get_extension_data(x11.conn, &xcb_input_id);
    if (!x11.ext_xi2 || !x11.ext_xi2->present) {
      LOG(QFATAL) << "could nolt set up XI2 extension";
    }

    struct {
      xcb_input_event_mask_t event_mask;
      uint32_t mask_bits;
    } mask;

    mask.event_mask.deviceid = XCB_INPUT_DEVICE_ALL_MASTER;
    mask.event_mask.mask_len = 1;
    mask.mask_bits = XCB_INPUT_XI_EVENT_MASK_RAW_MOTION;

    if (xcb_request_check(x11.conn,
                          xcb_input_xi_select_events_checked(x11.conn, x11.screen->root, 1, &mask.event_mask))) {
      LOG(QFATAL) << "could not setup XI2 extension";
    }
  }
}

xcb_atom_t X11_InternAtom(xcb_connection_t* conn, std::string_view name,

                          bool only_if_exists) {
  xcb_intern_atom_reply_t* reply =
      xcb_intern_atom_reply(conn, xcb_intern_atom(conn, only_if_exists, name.size(), name.data()), nullptr);
  absl::Cleanup reply_freer = [reply] {
    if (reply) free(reply);
  };

  if (!reply || !reply->atom) {
    LOG(FATAL) << "could not intern atom " << name;
  }

  return reply->atom;
}

void X11_SendClientMessage32(xcb_window_t window, xcb_atom_t type, xcb_atom_t arg1, uint32_t arg2, uint32_t arg3,
                             uint32_t arg4) {
  xcb_client_message_event_t event = {
      .response_type = XCB_CLIENT_MESSAGE,
      .format = 32,
      .window = window,
      .type = type,
      .data = {.data32 = {arg1, arg2, arg3, arg4}},
  };

  xcb_send_event(x11.conn, false, window, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char*>(&event));
}

void X11_Send_WM_Take_Focus(xcb_window_t window, uint32_t time) {
  X11_SendClientMessage32(window, x11.atoms.wm_protocols, x11.atoms.wm_take_focus, time, 0, 0);
}

void X11_Send_WM_Delete_Window(xcb_window_t window) {
  X11_SendClientMessage32(window, x11.atoms.wm_protocols, x11.atoms.wm_delete_window, XCB_CURRENT_TIME, 0, 0);
}

void X11_SendConfigureNotify(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
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

  xcb_send_event(x11.conn, false, window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, reinterpret_cast<const char*>(&event));
}

//

bool X11_InitializeKeyResolver(X11_KeyResolver& key_resolver) {
  key_resolver.ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!key_resolver.ctx) return false;

  const int32_t device_id = xkb_x11_get_core_keyboard_device_id(x11.conn);
  if (device_id == -1) return false;

  key_resolver.keymap =
      xkb_x11_keymap_new_from_device(key_resolver.ctx, x11.conn, device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!key_resolver.keymap) return false;

  return true;
}

void X11_FreeKeyResolver(X11_KeyResolver& key_resolver) {
  xkb_keymap_unref(key_resolver.keymap);
  xkb_context_unref(key_resolver.ctx);
}

xcb_keycode_t X11_ResolveKeyCode(const X11_KeyResolver& key_resolver, std::string_view keyname) {
  const xkb_keycode_t keycode = xkb_keymap_key_by_name(key_resolver.keymap, keyname.data());
  CHECK(xkb_keycode_is_legal_x11(keycode));
  return keycode;
}

}  // namespace nyla