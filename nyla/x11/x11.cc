#include "nyla/x11/x11.h"

#include <string_view>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xkbcommon/xkbcommon-x11.h"
#include "xkbcommon/xkbcommon.h"

namespace nyla {

X11State x11;

void InitializeX11() {
  int iscreen;
  x11.conn = xcb_connect(nullptr, &iscreen);
  if (xcb_connection_has_error(x11.conn))
    LOG(QFATAL) << "could not connect to X server";

  x11.screen = xcb_aux_get_screen(x11.conn, iscreen);
  CHECK(x11.screen);

  x11.atoms.compound_text = InternAtom(x11.conn, "COMPOUND_TEXT");
  x11.atoms.wm_delete_window = InternAtom(x11.conn, "WM_DELETE_WINDOW");
  x11.atoms.wm_name = InternAtom(x11.conn, "WM_NAME");
  x11.atoms.wm_protocols = InternAtom(x11.conn, "WM_PROTOCOLS");
  x11.atoms.wm_state = InternAtom(x11.conn, "WM_STATE");
  x11.atoms.wm_take_focus = InternAtom(x11.conn, "WM_TAKE_FOCUS");

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

xcb_atom_t InternAtom(xcb_connection_t* conn, std::string_view name,
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

//

bool InitializeKeyResolver(KeyResolver& resolver) {
  resolver.ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!resolver.ctx) return false;

  const int32_t device_id = xkb_x11_get_core_keyboard_device_id(x11.conn);
  if (device_id == -1) return false;

  resolver.keymap = xkb_x11_keymap_new_from_device(
      resolver.ctx, x11.conn, device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!resolver.keymap) return false;

  return true;
}

void FreeKeyResolver(KeyResolver& resolver) {
  xkb_keymap_unref(resolver.keymap);
  xkb_context_unref(resolver.ctx);
}

xcb_keycode_t ResolveKeyCode(const KeyResolver& resolver,
                             std::string_view keyname) {
  const xkb_keycode_t keycode =
      xkb_keymap_key_by_name(resolver.keymap, keyname.data());
  CHECK(xkb_keycode_is_legal_x11(keycode));
  return keycode;
}

}  // namespace nyla
