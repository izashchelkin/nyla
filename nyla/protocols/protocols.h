#ifndef NYLA_PROTOCOLS_H
#define NYLA_PROTOCOLS_H

#include <string_view>

#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

xcb_atom_t InternAtom(xcb_connection_t* conn, std::string_view name,
                      bool only_if_exists = false);

struct Atoms {
  xcb_atom_t wm_protocols;
  xcb_atom_t wm_delete_window;
};

Atoms InternAtoms(xcb_connection_t* conn);

void WMDeleteWindow(xcb_connection_t* conn, xcb_window_t window,
                      const Atoms& atoms);

}  // namespace nyla

#endif
