#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

void SendClientMessage32(xcb_connection_t* conn, xcb_window_t window,
                         xcb_atom_t type, xcb_atom_t arg1, uint32_t arg2,
                         uint32_t arg3, uint32_t arg4);

}  // namespace nyla
