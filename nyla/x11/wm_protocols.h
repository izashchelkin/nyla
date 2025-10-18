#include "xcb/xproto.h"

namespace nyla {

void Send_WM_Take_Focus(xcb_window_t window, uint32_t time);

void Send_WM_Delete_Window(xcb_window_t window);

}  // namespace nyla
