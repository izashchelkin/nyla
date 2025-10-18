#include "nyla/x11/x11.h"

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"

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
}

xcb_atom_t InternAtom(xcb_connection_t* conn, std::string_view name,
                      bool only_if_exists) {
  xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
      conn, xcb_intern_atom(conn, only_if_exists, name.size(), name.data()),
      nullptr);
  absl::Cleanup reply_freer = [reply] {
    if (reply) free(reply);
  };

  if (reply && reply->atom) {
    LOG(INFO) << "interned atom " << name << " = " << reply->atom;
    return reply->atom;
  }

  LOG(FATAL) << "could not intern atom " << name;
}

}  // namespace nyla
