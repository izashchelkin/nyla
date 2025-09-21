#include "nyla/protocols/wm_protocols.h"

#include "absl/log/check.h"
#include "xcb/xcb.h"

namespace nyla {

void Send_WM_Take_Focus(xcb_connection_t* conn, xcb_window_t window,
                        const Atoms& atoms, uint32_t time) {
  CHECK_NE(time, XCB_CURRENT_TIME);
  SendClientMessage(conn, window, atoms.wm_protocols, atoms.wm_take_focus,
                    time);
}

void Send_WM_Delete_Window(xcb_connection_t* conn, xcb_window_t window,
                           const Atoms& atoms) {
  SendClientMessage(conn, window, atoms.wm_protocols, atoms.wm_delete_window,
                    XCB_CURRENT_TIME);
}

}  // namespace nyla
