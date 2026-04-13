#pragma once

#include <cstdint>

#include "nyla/commons/platform.h"
#include <sys/mman.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xkbcommon/xkbcommon-x11.h>

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
    X(compound_text)                                                                                                   \
    X(wm_delete_window)                                                                                                \
    X(wm_protocols)                                                                                                    \
    X(wm_name)                                                                                                         \
    X(wm_state)                                                                                                        \
    X(wm_take_focus)
// NOLINTEND

class LinuxX11Platform
{
  public:
    struct Atoms
    {
#define X(name) xcb_atom_t name;
        NYLA_X11_ATOMS(X)
#undef X
    };

    static auto GetConn() -> xcb_connection_t *;
    static auto GetScreenIndex() -> int;
    static auto GetScreen() -> xcb_screen_t *;
    static auto GetRoot() -> xcb_window_t;

    static void Grab();
    static void Ungrab();
    static void Flush();

    static auto GetXInputExtensionMajorOpCode() -> uint32_t;

    static auto GetAtoms() -> LinuxX11Platform::Atoms &;
    static auto InternAtom(byteview name, bool onlyIfExists) -> xcb_atom_t;

    static auto CreateWin(uint32_t width, uint32_t height, bool overrideRedirect, xcb_event_mask_t eventMask)
        -> xcb_window_t;
    static auto WinGetHandle() -> xcb_window_t;
    static void SetWindow(xcb_window_t window);

    static void SendClientMessage32(xcb_window_t window, xcb_atom_t type, xcb_atom_t arg1, uint32_t arg2, uint32_t arg3,
                                    uint32_t arg4);
    static void SendWmTakeFocus(xcb_window_t window, uint32_t time);
    static void SendWmDeleteWindow(xcb_window_t window);
    static void SendConfigureNotify(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
                                    uint16_t height, uint16_t borderWidth);

    static auto ConvertKeyPhysicalIntoXkbName(KeyPhysical key) -> const char *;
    static auto KeyPhysicalToKeyCode(KeyPhysical key) -> uint32_t;
    static auto KeyCodeToKeyPhysical(uint32_t keyCode, KeyPhysical *outKeyPhysical) -> bool;
};

//
} // namespace nyla