#pragma once

#include <string_view>

#include "nyla/platform/key_physical.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"
#include "xkbcommon/xkbcommon.h"

namespace nyla::platform_x11_internal
{

// NOLINTBEGIN
#define Nyla_X11_Atoms(X)                                                                                              \
    X(compound_text)                                                                                                   \
    X(wm_delete_window)                                                                                                \
    X(wm_protocols)                                                                                                    \
    X(wm_name)                                                                                                         \
    X(wm_state)                                                                                                        \
    X(wm_take_focus)
// NOLINTEND

struct X11State
{
    xcb_connection_t *conn;
    xcb_screen_t *screen;

    const xcb_query_extension_reply_t *extXi2;

    struct
    {
#define X(atom) xcb_atom_t atom;
        Nyla_X11_Atoms(X)
#undef X
    } atoms;
};
extern X11State x11;

void X11Initialize(bool keyboardInput, bool mouseInput);

auto X11CreateWindow(uint32_t width, uint32_t height, bool overrideRedirect, xcb_event_mask_t eventMask)
    -> xcb_window_t;

void X11Flush();

auto X11InternAtom(xcb_connection_t *conn, std::string_view name, bool onlyIfExists = false) -> xcb_atom_t;

void X11SendClientMessage32(xcb_window_t window, xcb_atom_t type, xcb_atom_t arg1, uint32_t arg2, uint32_t arg3,
                            uint32_t arg4);

void X11SendWmTakeFocus(xcb_window_t window, uint32_t time);

void X11SendWmDeleteWindow(xcb_window_t window);

void X11SendConfigureNotify(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
                            uint16_t height, uint16_t borderWidth);

//

struct X11KeyResolver
{
    xkb_context *ctx;
    xkb_keymap *keymap;
};

auto X11InitializeKeyResolver(X11KeyResolver &resolver) -> bool;
void X11FreeKeyResolver(X11KeyResolver &resolver);
auto X11ResolveKeyCode(const X11KeyResolver &resolver, std::string_view keyname) -> xcb_keycode_t;

auto ConvertKeyPhysicalIntoXkbName(KeyPhysical key) -> const char *;

} // namespace nyla::platform_x11_internal