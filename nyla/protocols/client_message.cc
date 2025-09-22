#include "nyla/protocols/client_message.h"

#include <cstdint>

#include "xcb/xproto.h"

namespace nyla {

void SendClientMessage32(xcb_connection_t* conn, xcb_window_t window,
                         xcb_atom_t type, xcb_atom_t arg1, uint32_t arg2,
                         uint32_t arg3, uint32_t arg4) {
  xcb_client_message_event_t event = {
      .response_type = XCB_CLIENT_MESSAGE,
      .format = 32,
      .window = window,
      .type = type,
      .data = {.data32 = {arg1, arg2, arg3, arg4}}};

  xcb_send_event(conn, false, window, XCB_EVENT_MASK_NO_EVENT,
                 reinterpret_cast<const char*>(&event));
}

}  // namespace nyla
