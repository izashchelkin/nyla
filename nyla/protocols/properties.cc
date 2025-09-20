#include "nyla/protocols/properties.h"

namespace nyla {

xcb_get_property_reply_t* FetchPropertyInternal(xcb_connection_t* conn,
                                                xcb_window_t window,
                                                const xcb_atom_t property,
                                                const xcb_atom_t type,
                                                const uint32_t long_length) {
  xcb_get_property_reply_t* reply = xcb_get_property_reply(
      conn,
      xcb_get_property(conn, false, window, property, type, 0, long_length),
      nullptr);

  if (reply->type != type) {
    LOG(FATAL) << "FetchPropertyInternal: mismatched type";
  }

  return reply;
}

}  // namespace nyla
