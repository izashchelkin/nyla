#include "nyla/protocols/wm_protocols.h"

#include "xcb/xcb.h"

namespace nyla {

void Send_WM_Take_Focus(xcb_connection_t* conn, xcb_window_t window,
                        const Atoms& atoms, uint32_t time) {
  SendClientMessage32(conn, window, atoms.wm_protocols, atoms.wm_take_focus,
                      time, 0, 0);
}

void Send_WM_Delete_Window(xcb_connection_t* conn, xcb_window_t window,
                           const Atoms& atoms) {
  SendClientMessage32(conn, window, atoms.wm_protocols, atoms.wm_delete_window,
                      XCB_CURRENT_TIME, 0, 0);
}

}  // namespace nyla
