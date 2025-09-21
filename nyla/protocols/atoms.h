#pragma once

#include <string_view>

#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

xcb_atom_t InternAtom(xcb_connection_t* conn, std::string_view name,
                      bool only_if_exists = false);

struct Atoms {
  xcb_atom_t wm_delete_window;
  xcb_atom_t wm_protocols;
  xcb_atom_t wm_state;
  xcb_atom_t wm_take_focus;
};

Atoms InternAtoms(xcb_connection_t* conn);

}  // namespace nyla
