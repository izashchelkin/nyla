#include "nyla/commons/macros.h"
#include "nyla/commons/platform_linux.h"

#include <cstdlib>

#include "nyla/commons/array.h"
#include "nyla/commons/fmt.h"

namespace nyla
{

auto API X11GetConn() -> xcb_connection_t *
{
    return platform->x11.conn;
}

auto API X11GetScreenIndex() -> int
{
    return platform->x11.screenIndex;
}

auto API X11GetScreen() -> xcb_screen_t *
{
    return platform->x11.screen;
}

auto API X11GetRoot() -> xcb_window_t
{
    return X11GetScreen()->root;
}

void API X11Grab()
{
    xcb_grab_server(X11GetConn());
}

void API X11Ungrab()
{
    xcb_ungrab_server(X11GetConn());
}

void API X11Flush()
{
    xcb_flush(X11GetConn());
}

auto API X11GetXInputExtensionMajorOpCode() -> uint32_t
{
    return platform->x11.extensionXInput2MajorOpCode;
}

auto API X11GetAtoms() -> const x11_atoms &
{
    return platform->x11.atoms;
}

auto API X11InternAtom(byteview name, bool onlyIfExists) -> xcb_atom_t
{
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
        platform->x11.conn, xcb_intern_atom(platform->x11.conn, onlyIfExists, name.size, (char *)name.data), nullptr);
    if (!reply || !reply->atom)
        LOG("could not intern atom " SV_FMT, SV_ARG(name));

    auto ret = reply->atom;
    free(reply);
    return ret;
}

auto API X11CreateWin(uint32_t width, uint32_t height, bool overrideRedirect, xcb_event_mask_t eventMask)
    -> xcb_window_t
{
    const xcb_window_t window = xcb_generate_id(platform->x11.conn);

    const array<uint32_t, 2> values{overrideRedirect, eventMask};
    xcb_create_window(platform->x11.conn, XCB_COPY_FROM_PARENT, window, X11GetRoot(), 0, 0, width, height, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, X11GetScreen()->root_visual,
                      XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, values.data);

    xcb_map_window(X11GetConn(), window);
    xcb_flush(X11GetConn());

    return window;
}

auto API X11WinGetHandle() -> xcb_window_t
{
    return platform->x11.win;
}

void API X11SetWindow(xcb_window_t window)
{
    platform->x11.win = window;
}

void API X11SendClientMessage32(xcb_window_t window, xcb_atom_t type, xcb_atom_t arg1, uint32_t arg2, uint32_t arg3,
                                uint32_t arg4)
{
    const xcb_client_message_event_t event = {
        .response_type = XCB_CLIENT_MESSAGE,
        .format = 32,
        .window = window,
        .type = type,
        .data = {.data32 = {arg1, arg2, arg3, arg4}},
    };
    xcb_send_event(X11GetConn(), false, window, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char *>(&event));
}

void API X11SendWmTakeFocus(xcb_window_t window, uint32_t time)
{
    X11SendClientMessage32(window, X11GetAtoms().wm_protocols, X11GetAtoms().wm_take_focus, time, 0, 0);
}

void API X11SendWmDeleteWindow(xcb_window_t window)
{
    X11SendClientMessage32(window, X11GetAtoms().wm_protocols, X11GetAtoms().wm_delete_window, XCB_CURRENT_TIME, 0, 0);
}

void API X11SendConfigureNotify(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
                                uint16_t height, uint16_t borderWidth)
{
    const xcb_configure_notify_event_t event = {
        .response_type = XCB_CONFIGURE_NOTIFY,
        .window = window,
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .border_width = borderWidth,
    };
    xcb_send_event(X11GetConn(), false, window, XCB_EVENT_MASK_STRUCTURE_NOTIFY,
                   reinterpret_cast<const char *>(&event));
}

auto API X11KeyPhysicalToKeyCode(KeyPhysical key) -> uint32_t
{
    return platform->x11.keyCodes[static_cast<uint32_t>(key)];
}

auto API X11KeyCodeToKeyPhysical(uint32_t keyCode, KeyPhysical *outKeyPhysical) -> bool
{
    for (uint32_t i = 1; i < Array::Size(platform->x11.keyCodes); ++i)
    {
        if (platform->x11.keyCodes[i] == keyCode)
        {
            *outKeyPhysical = static_cast<KeyPhysical>(i);
            return true;
        }
    }
    return false;
}

auto API X11ConvertKeyPhysicalIntoXkbName(KeyPhysical key) -> byteview
{
    using K = KeyPhysical;

    switch (key)
    {
    case K::Escape:
        return "ESC"_s;
    case K::Grave:
        return "TLDE"_s;

    case K::Digit1:
        return "AE01"_s;
    case K::Digit2:
        return "AE02"_s;
    case K::Digit3:
        return "AE03"_s;
    case K::Digit4:
        return "AE04"_s;
    case K::Digit5:
        return "AE05"_s;
    case K::Digit6:
        return "AE06"_s;
    case K::Digit7:
        return "AE07"_s;
    case K::Digit8:
        return "AE08"_s;
    case K::Digit9:
        return "AE09"_s;
    case K::Digit0:
        return "AE10"_s;
    case K::Minus:
        return "AE11"_s;
    case K::Equal:
        return "AE12"_s;
    case K::Backspace:
        return "BKSP"_s;

    case K::Tab:
        return "TAB"_s;
    case K::Q:
        return "AD01"_s;
    case K::W:
        return "AD02"_s;
    case K::E:
        return "AD03"_s;
    case K::R:
        return "AD04"_s;
    case K::T:
        return "AD05"_s;
    case K::Y:
        return "AD06"_s;
    case K::U:
        return "AD07"_s;
    case K::I:
        return "AD08"_s;
    case K::O:
        return "AD09"_s;
    case K::P:
        return "AD10"_s;
    case K::LeftBracket:
        return "AD11"_s;
    case K::RightBracket:
        return "AD12"_s;
    case K::Enter:
        return "RTRN"_s;

    case K::CapsLock:
        return "CAPS"_s;
    case K::A:
        return "AC01"_s;
    case K::S:
        return "AC02"_s;
    case K::D:
        return "AC03"_s;
    case K::F:
        return "AC04"_s;
    case K::G:
        return "AC05"_s;
    case K::H:
        return "AC06"_s;
    case K::J:
        return "AC07"_s;
    case K::K:
        return "AC08"_s;
    case K::L:
        return "AC09"_s;
    case K::Semicolon:
        return "AC10"_s;
    case K::Apostrophe:
        return "AC11"_s;

    case K::LeftShift:
        return "LFSH"_s;
    case K::Z:
        return "AB01"_s;
    case K::X:
        return "AB02"_s;
    case K::C:
        return "AB03"_s;
    case K::V:
        return "AB04"_s;
    case K::B:
        return "AB05"_s;
    case K::N:
        return "AB06"_s;
    case K::M:
        return "AB07"_s;
    case K::Comma:
        return "AB08"_s;
    case K::Period:
        return "AB09"_s;
    case K::Slash:
        return "AB10"_s;
    case K::RightShift:
        return "RTSH"_s;

    case K::LeftCtrl:
        return "LCTL"_s;
    case K::LeftAlt:
        return "LALT"_s;
    case K::Space:
        return "SPCE"_s;
    case K::RightAlt:
        return "RALT"_s;
    case K::RightCtrl:
        return "RCTL"_s;

    case K::ArrowLeft:
        return "LEFT"_s;
    case K::ArrowRight:
        return "RGHT"_s;
    case K::ArrowUp:
        return "UP"_s;
    case K::ArrowDown:
        return "DOWN"_s;

    case K::F1:
        return "FK01"_s;
    case K::F2:
        return "FK02"_s;
    case K::F3:
        return "FK03"_s;
    case K::F4:
        return "FK04"_s;
    case K::F5:
        return "FK05"_s;
    case K::F6:
        return "FK06"_s;
    case K::F7:
        return "FK07"_s;
    case K::F8:
        return "FK08"_s;
    case K::F9:
        return "FK09"_s;
    case K::F10:
        return "FK10"_s;
    case K::F11:
        return "FK11"_s;
    case K::F12:
        return "FK12"_s;

    default:
        TRAP();
        UNREACHABLE();h
    }
}

} // namespace nyla
