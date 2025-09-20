#include "nyla/protocols/wm_protocols.h"

namespace nyla {

void SendClientMessage(xcb_connection_t* conn, xcb_window_t window,
                       xcb_atom_t type, xcb_atom_t arg1, uint32_t time) {
  xcb_client_message_event_t event = {};
  event.response_type = XCB_CLIENT_MESSAGE;
  event.window = window;
  event.type = type;
  event.format = 32;
  event.data.data32[0] = arg1;
  event.data.data32[1] = time;

  xcb_send_event(conn, /*propagate=*/false, window, XCB_EVENT_MASK_NO_EVENT,
                 reinterpret_cast<const char*>(&event));
}

}  // namespace nyla
