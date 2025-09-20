#include "nyla/protocols/atoms.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

void SendClientMessage(xcb_connection_t* conn, xcb_window_t window,
                       xcb_atom_t type, xcb_atom_t arg1, uint32_t time);

inline void Send_WM_Take_Focus(xcb_connection_t* conn, xcb_window_t window,
                               const Atoms& atoms, uint32_t time) {
  SendClientMessage(conn, window, atoms.wm_protocols, atoms.wm_take_focus,
                    time);
}

inline void Send_WM_Delete_Window(xcb_connection_t* conn, xcb_window_t window,
                                  const Atoms& atoms) {
  SendClientMessage(conn, window, atoms.wm_protocols, atoms.wm_delete_window,
                    XCB_CURRENT_TIME);
}

}  // namespace nyla
