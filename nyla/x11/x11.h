#pragma once

#include <string_view>

#include "xcb/xcb.h"
#include "xcb/xproto.h"
#include "xkbcommon/xkbcommon.h"

namespace nyla {

struct X11State {
  xcb_connection_t* conn;
  xcb_screen_t* screen;

  struct {
    xcb_atom_t compound_text;
    xcb_atom_t wm_delete_window;
    xcb_atom_t wm_name;
    xcb_atom_t wm_protocols;
    xcb_atom_t wm_state;
    xcb_atom_t wm_take_focus;
  } atoms;
};
extern X11State x11;

void InitializeX11();

xcb_atom_t InternAtom(xcb_connection_t* conn, std::string_view name,
                      bool only_if_exists = false);

//

struct KeyResolver {
  xkb_context* ctx;
  xkb_keymap* keymap;
};

bool InitializeKeyResolver(KeyResolver& resolver);
void FreeKeyResolver(KeyResolver& resolver);
xcb_keycode_t ResolveKeyCode(const KeyResolver& resolver,
                             std::string_view keyname);

}  // namespace nyla
