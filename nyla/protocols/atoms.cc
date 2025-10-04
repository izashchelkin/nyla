#include "nyla/protocols/atoms.h"

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"

namespace nyla {

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

Atoms InternAtoms(xcb_connection_t* conn) {
  return Atoms{
      .wm_delete_window = InternAtom(conn, "WM_DELETE_WINDOW"),
      .wm_name = InternAtom(conn, "WM_NAME"),
      .wm_protocols = InternAtom(conn, "WM_PROTOCOLS"),
      .wm_state = InternAtom(conn, "WM_STATE"),
      .wm_take_focus = InternAtom(conn, "WM_TAKE_FOCUS"),
  };
}

}  // namespace nyla
