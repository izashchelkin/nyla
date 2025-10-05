#include "nyla/protocols/atoms.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

void Send_WM_Take_Focus(xcb_connection_t* conn, xcb_window_t window,
                        const Atoms* atoms, uint32_t time);

void Send_WM_Delete_Window(xcb_connection_t* conn, xcb_window_t window,
                           const Atoms* atoms);

}  // namespace nyla
