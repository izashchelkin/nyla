#pragma once

#include <string_view>

#include "xcb/xcb.h"
#include "xcb/xproto.h"
#include "xkbcommon/xkbcommon.h"

namespace nyla {

#define Nyla_X11_Atoms(X) \
  X(compound_text)        \
  X(wm_delete_window)     \
  X(wm_protocols)         \
  X(wm_name)              \
  X(wm_state)             \
  X(wm_take_focus)

struct X11_State {
  xcb_connection_t* conn;
  xcb_screen_t* screen;

  const xcb_query_extension_reply_t* ext_xi2;

  struct {
#define X(atom) xcb_atom_t atom;
    Nyla_X11_Atoms(X)
#undef X
  } atoms;
};
extern X11_State x11;

void X11_Initialize();

xcb_window_t X11_CreateWindow(uint32_t width, uint32_t height, bool override_redirect, xcb_event_mask_t event_mask);

void X11_Flush();

xcb_atom_t X11_InternAtom(xcb_connection_t* conn, std::string_view name, bool only_if_exists = false);

void X11_SendClientMessage32(xcb_window_t window, xcb_atom_t type, xcb_atom_t arg1, uint32_t arg2, uint32_t arg3,
                             uint32_t arg4);

void X11_Send_WM_Take_Focus(xcb_window_t window, uint32_t time);

void X11_Send_WM_Delete_Window(xcb_window_t window);

void X11_SendConfigureNotify(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
                             uint16_t height, uint16_t border_width);

//

struct X11_KeyResolver {
  xkb_context* ctx;
  xkb_keymap* keymap;
};

bool X11_InitializeKeyResolver(X11_KeyResolver& resolver);
void X11_FreeKeyResolver(X11_KeyResolver& resolver);
xcb_keycode_t X11_ResolveKeyCode(const X11_KeyResolver& resolver, std::string_view keyname);

}  // namespace nyla