#include "nyla/x11/send.h"

#include <cstdint>

#include "nyla/x11/x11.h"
#include "xcb/xproto.h"

namespace nyla {

void SendClientMessage32(xcb_window_t window, xcb_atom_t type, xcb_atom_t arg1,
                         uint32_t arg2, uint32_t arg3, uint32_t arg4) {
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

void SendConfigureNotify(xcb_window_t window, xcb_window_t parent, int16_t x,
                         int16_t y, uint16_t width, uint16_t height,
                         uint16_t border_width) {
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

}  // namespace nyla
