#include "nyla/wm/protocols.h"

#include <string_view>

#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

xcb_atom_t InternAtom(xcb_connection_t* conn, std::string_view name,
                      bool only_if_exists) {
  xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(
      conn, xcb_intern_atom(conn, only_if_exists, name.size(), name.data()),
      nullptr);

  return reply ? reply->atom : XCB_NONE;
}

Atoms InternAtoms(xcb_connection_t* conn) {
  return Atoms{
      .wm_protocols = InternAtom(conn, "WM_PROTOCOLS"),
      .wm_delete_window = InternAtom(conn, "WM_DELETE_WINDOW"),
  };
}

void SendDeleteWindow(xcb_connection_t* conn, xcb_window_t window,
                      const Atoms& atoms) {
  xcb_client_message_event_t event;
  event.response_type = XCB_CLIENT_MESSAGE;
  event.window = window;
  event.type = atoms.wm_protocols;
  event.format = 32;
  event.data.data32[0] = atoms.wm_delete_window;
  event.data.data32[1] = XCB_CURRENT_TIME;

  xcb_send_event(conn, /*propagate=*/false, window, XCB_EVENT_MASK_NO_EVENT,
                 reinterpret_cast<const char*>(&event));
}

}  // namespace nyla
