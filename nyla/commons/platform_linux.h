#pragma once

#include <cerrno>
#include <cmath>
#include <cstdint>

#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/signal.h>

#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_errors.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_util.h>
#include <xcb/xinput.h>
#include <xcb/xproto.h>
#include <xkbcommon/xkbcommon-x11.h>

#include "nyla/commons/array_def.h"
#include "nyla/commons/gamepad.h" // IWYU pragma: export
#include "nyla/commons/keyboard.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/time.h" // IWYU pragma: export

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define explicit explicit_
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <xcb/xkb.h>

#undef explicit

namespace nyla
{

// NOLINTBEGIN
#define NYLA_X11_ATOMS(X)                                                                                              \
    X(compound_text, COMPOUND_TEXT)                                                                                    \
    X(wm_delete_window, WM_DELETE_WINDOW)                                                                              \
    X(wm_protocols, WM_PROTOCOLS)                                                                                      \
    X(wm_name, WM_NAME)                                                                                                \
    X(wm_state, WM_STATE)                                                                                              \
    X(wm_take_focus, WM_TAKE_FOCUS)
// NOLINTEND

struct x11_atoms
{
#define X(varname, atomname) xcb_atom_t varname;
    NYLA_X11_ATOMS(X)
#undef X
};

struct platform_state
{
    struct
    {
        xcb_connection_t *conn;
        xcb_screen_t *screen;
        uint32_t extensionXInput2MajorOpCode;
        int screenIndex;

        xcb_window_t win;
        xcb_get_geometry_reply_t winGeom;

        x11_atoms atoms;

        array<xcb_keycode_t, static_cast<uint32_t>(KeyPhysical::Count)> keyCodes;

        xcb_errors_context_t *errorsContext;
    } x11;
};
extern platform_state *platform;

auto API X11GetConn() -> xcb_connection_t *;
auto API X11GetScreenIndex() -> int;
auto API X11GetScreen() -> xcb_screen_t *;
auto API X11GetRoot() -> xcb_window_t;
void API X11Grab();
void API X11Ungrab();
void API X11Flush();
auto API X11GetXInputExtensionMajorOpCode() -> uint32_t;
auto API X11GetAtoms() -> const x11_atoms &;
auto API X11InternAtom(byteview name, bool onlyIfExists) -> xcb_atom_t;
auto API X11CreateWin(uint32_t width, uint32_t height, bool overrideRedirect, xcb_event_mask_t eventMask)
    -> xcb_window_t;
auto API X11WinGetHandle() -> xcb_window_t;
void API X11SetWindow(xcb_window_t window);
void API X11SendClientMessage32(xcb_window_t window, xcb_atom_t type, xcb_atom_t arg1, uint32_t arg2, uint32_t arg3,
                                uint32_t arg4);
void API X11SendWmTakeFocus(xcb_window_t window, uint32_t time);
void API X11SendWmDeleteWindow(xcb_window_t window);
void API X11SendConfigureNotify(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
                                uint16_t height, uint16_t borderWidth);
auto API X11ConvertKeyPhysicalIntoXkbName(KeyPhysical key) -> byteview;
auto API X11KeyPhysicalToKeyCode(KeyPhysical key) -> uint32_t;
auto API X11KeyCodeToKeyPhysical(uint32_t keyCode, KeyPhysical *outKeyPhysical) -> bool;

//
} // namespace nyla