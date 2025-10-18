#include "nyla/x11/wm_protocols.h"

#include "nyla/x11/send.h"
#include "nyla/x11/x11.h"
#include "xcb/xcb.h"

namespace nyla {

void Send_WM_Take_Focus(xcb_window_t window, uint32_t time) {
  SendClientMessage32(window, x11.atoms.wm_protocols, x11.atoms.wm_take_focus,
                      time, 0, 0);
}

void Send_WM_Delete_Window(xcb_window_t window) {
  SendClientMessage32(window, x11.atoms.wm_protocols,
                      x11.atoms.wm_delete_window, XCB_CURRENT_TIME, 0, 0);
}

}  // namespace nyla
