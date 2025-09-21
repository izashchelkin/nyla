#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

void SendClientMessage(xcb_connection_t* conn, xcb_window_t window,
                       xcb_atom_t type, xcb_atom_t arg1, uint32_t time);

}  // namespace nyla
