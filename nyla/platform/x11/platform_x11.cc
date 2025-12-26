#include "nyla/platform/x11/platform_x11.h"

#include <string>
#include <sys/inotify.h>
#include <unistd.h>

#include <cstdint>
#include <string_view>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "nyla/commons/containers/set.h"
#include "nyla/commons/memory/optional.h"
#include "nyla/commons/os/clock.h"
#include "nyla/platform/key_physical.h"
#include "nyla/platform/platform.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xcb/xinput.h"
#include "xcb/xproto.h"
#include "xkbcommon/xkbcommon-x11.h"
#include "xkbcommon/xkbcommon.h"

// Source - https://stackoverflow.com/a/72177598
// Posted by HolyBlackCat, modified by community. See post 'Timeline' for change history
// Retrieved 2025-11-17, License - CC BY-SA 4.0

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

using namespace platform_x11_internal;

namespace
{

bool shouldExit = false;

}

void PlatformInit(PlatformInitDesc desc)
{
    X11Initialize(desc.keyboardInput, desc.mouseInput);
}

static X11KeyResolver g_KeyResolver;

void PlatformInputResolverBegin()
{
    g_KeyResolver = {};
    X11InitializeKeyResolver(g_KeyResolver);
}

auto PlatformInputResolve(KeyPhysical key) -> uint32_t
{
    const char *xkbName = ConvertKeyPhysicalIntoXkbName(key);
    const uint32_t keycode = X11ResolveKeyCode(g_KeyResolver, xkbName);
    return keycode;
}

void PlatformInputResolverEnd()
{
    X11FreeKeyResolver(g_KeyResolver);
    g_KeyResolver = {};
}

auto PlatformCreateWindow() -> PlatformWindow
{
    xcb_window_t window = xcb_generate_id(x11.conn);

    CHECK(!xcb_request_check(
        x11.conn, xcb_create_window_checked(
                      x11.conn, XCB_COPY_FROM_PARENT, window, x11.screen->root, 0, 0, x11.screen->width_in_pixels,
                      x11.screen->height_in_pixels, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, x11.screen->root_visual,
                      XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
                      (uint32_t[]){false, XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                                              XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE})));

    xcb_map_window(x11.conn, window);
    xcb_flush(x11.conn);

    return {window};
}

auto PlatformGetWindowSize(PlatformWindow window) -> PlatformWindowSize
{
    const xcb_get_geometry_reply_t *windowGeometry =
        xcb_get_geometry_reply(x11.conn, xcb_get_geometry(x11.conn, window.handle), nullptr);
    return {.width = windowGeometry->width, .height = windowGeometry->height};
}

namespace
{

int fsNotifyFd = 0;

std::array<PlatformFileChanged, 16> fileChanges;
uint32_t fileChangesIndex = 0;
Set<std::string> filesWatched;

} // namespace

void PlatformFsWatchFile(const std::string &path)
{
    if (!fsNotifyFd)
    {
        fsNotifyFd = inotify_init1(IN_NONBLOCK);
        CHECK_GT(fsNotifyFd, 0);
    }

    CHECK_LE(filesWatched.size(), fileChanges.size());

    if (auto [it, ok] = filesWatched.emplace(path); ok)
        inotify_add_watch(fsNotifyFd, path.c_str(), IN_CLOSE);
}

auto PlatformFsGetFileChanges() -> std::span<PlatformFileChanged>
{
    return fileChanges;
}

auto PlatformProcessEvents(const PlatformProcessEventsCallbacks &callbacks, void *user) -> PlatformProcessEventsResult
{
    if (fsNotifyFd)
    {
        char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
        char *bufp = buf;

        int numread;
        while ((numread = read(fsNotifyFd, buf, sizeof(buf))) > 0)
        {
            while (bufp != buf + numread)
            {
                const auto *event = reinterpret_cast<inotify_event *>(bufp);
                bufp += sizeof(inotify_event);

                if (event->mask & IN_ISDIR)
                {
                    bufp += event->len;
                    continue;
                }

                std::string path = {bufp, strlen(bufp)};
                bufp += event->len;

                fileChanges[fileChangesIndex] = {
                    .seen = false,
                    .path = path,
                };
                fileChangesIndex = (fileChangesIndex + 1) % fileChanges.size();
            }
        }
    }

    PlatformProcessEventsResult ret{};

    for (;;)
    {
        if (xcb_connection_has_error(x11.conn))
        {
            shouldExit = true;
            break;
        }

        xcb_generic_event_t *event = xcb_poll_for_event(x11.conn);
        if (!event)
            break;

        absl::Cleanup eventFreer = [=]() -> void { free(event); };
        const uint8_t eventType = event->response_type & 0x7F;

        switch (eventType)
        {
        case XCB_KEY_PRESS: {
            auto keypress = reinterpret_cast<xcb_key_press_event_t *>(event);
            if (callbacks.handleKeyPress)
                callbacks.handleKeyPress(user, keypress->detail);
            break;
        }

        case XCB_KEY_RELEASE: {
            auto keyrelease = reinterpret_cast<xcb_key_release_event_t *>(event);
            if (callbacks.handleKeyRelease)
                callbacks.handleKeyRelease(user, keyrelease->detail);
            break;
        }

        case XCB_BUTTON_PRESS: {
            auto buttonpress = reinterpret_cast<xcb_button_press_event_t *>(event);
            if (callbacks.handleMousePress)
                callbacks.handleMousePress(user, buttonpress->detail);
            break;
        }

        case XCB_BUTTON_RELEASE: {
            auto buttonrelease = reinterpret_cast<xcb_button_release_event_t *>(event);
            if (callbacks.handleMouseRelease)
                callbacks.handleMouseRelease(user, buttonrelease->detail);
            break;
        }

        case XCB_EXPOSE: {
            ret.shouldRedraw = true;
            break;
        }

        case XCB_CLIENT_MESSAGE: {
            auto clientmessage = reinterpret_cast<xcb_client_message_event_t *>(event);

            if (clientmessage->format == 32 && clientmessage->type == x11.atoms.wm_protocols &&
                clientmessage->data.data32[0] == x11.atoms.wm_delete_window)
            {
                shouldExit = true;
            }
            break;
        }
        }
    }

    return ret;
}

auto PlatformShouldExit() -> bool
{
    return shouldExit;
}

namespace platform_x11_internal
{

auto ConvertKeyPhysicalIntoXkbName(KeyPhysical key) -> const char *
{
    using K = KeyPhysical;

    switch (key)
    {
    case K::Escape:
        return "ESC";
    case K::Grave:
        return "TLDE";

    case K::Digit1:
        return "AE01";
    case K::Digit2:
        return "AE02";
    case K::Digit3:
        return "AE03";
    case K::Digit4:
        return "AE04";
    case K::Digit5:
        return "AE05";
    case K::Digit6:
        return "AE06";
    case K::Digit7:
        return "AE07";
    case K::Digit8:
        return "AE08";
    case K::Digit9:
        return "AE09";
    case K::Digit0:
        return "AE10";
    case K::Minus:
        return "AE11";
    case K::Equal:
        return "AE12";
    case K::Backspace:
        return "BKSP";

    case K::Tab:
        return "TAB";
    case K::Q:
        return "AD01";
    case K::W:
        return "AD02";
    case K::E:
        return "AD03";
    case K::R:
        return "AD04";
    case K::T:
        return "AD05";
    case K::Y:
        return "AD06";
    case K::U:
        return "AD07";
    case K::I:
        return "AD08";
    case K::O:
        return "AD09";
    case K::P:
        return "AD10";
    case K::LeftBracket:
        return "AD11";
    case K::RightBracket:
        return "AD12";
    case K::Enter:
        return "RTRN";

    case K::CapsLock:
        return "CAPS";
    case K::A:
        return "AC01";
    case K::S:
        return "AC02";
    case K::D:
        return "AC03";
    case K::F:
        return "AC04";
    case K::G:
        return "AC05";
    case K::H:
        return "AC06";
    case K::J:
        return "AC07";
    case K::K:
        return "AC08";
    case K::L:
        return "AC09";
    case K::Semicolon:
        return "AC10";
    case K::Apostrophe:
        return "AC11";

    case K::LeftShift:
        return "LFSH";
    case K::Z:
        return "AB01";
    case K::X:
        return "AB02";
    case K::C:
        return "AB03";
    case K::V:
        return "AB04";
    case K::B:
        return "AB05";
    case K::N:
        return "AB06";
    case K::M:
        return "AB07";
    case K::Comma:
        return "AB08";
    case K::Period:
        return "AB09";
    case K::Slash:
        return "AB10";
    case K::RightShift:
        return "RTSH";

    case K::LeftCtrl:
        return "LCTL";
    case K::LeftAlt:
        return "LALT";
    case K::Space:
        return "SPCE";
    case K::RightAlt:
        return "RALT";
    case K::RightCtrl:
        return "RCTL";

    case K::ArrowLeft:
        return "LEFT";
    case K::ArrowRight:
        return "RGHT";
    case K::ArrowUp:
        return "UP";
    case K::ArrowDown:
        return "DOWN";

    case K::F1:
        return "FK01";
    case K::F2:
        return "FK02";
    case K::F3:
        return "FK03";
    case K::F4:
        return "FK04";
    case K::F5:
        return "FK05";
    case K::F6:
        return "FK06";
    case K::F7:
        return "FK07";
    case K::F8:
        return "FK08";
    case K::F9:
        return "FK09";
    case K::F10:
        return "FK10";
    case K::F11:
        return "FK11";
    case K::F12:
        return "FK12";

    case K::Unknown:
    default:
        return nullptr;
    }
}

X11State x11;

void X11Initialize(bool keyboardInput, bool mouseInput)
{
    int iscreen;
    x11.conn = xcb_connect(nullptr, &iscreen);
    if (xcb_connection_has_error(x11.conn))
    {
        LOG(QFATAL) << "could not connect to X server";
    }

    x11.screen = xcb_aux_get_screen(x11.conn, iscreen);
    CHECK(x11.screen);

    {
#define X(atom) x11.atoms.atom = X11InternAtom(x11.conn, absl::AsciiStrToUpper(#atom));
        Nyla_X11_Atoms(X)
#undef X
    }

    if (keyboardInput)
    {
        uint16_t majorXkbVersionOut, minorXkbVersionOut;
        uint8_t baseEventOut, baseErrorOut;

        if (!xkb_x11_setup_xkb_extension(x11.conn, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION,
                                         XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, &majorXkbVersionOut, &minorXkbVersionOut,
                                         &baseEventOut, &baseErrorOut))
        {
            LOG(QFATAL) << "could not set up xkb extension";
        }

        if (majorXkbVersionOut < XKB_X11_MIN_MAJOR_XKB_VERSION ||
            (majorXkbVersionOut == XKB_X11_MIN_MAJOR_XKB_VERSION && minorXkbVersionOut < XKB_X11_MIN_MINOR_XKB_VERSION))
        {
            LOG(QFATAL) << "could not set up xkb extension";
        }

        xcb_generic_error_t *err = nullptr;
        if (!Unown(xcb_xkb_per_client_flags_reply(
                x11.conn,
                xcb_xkb_per_client_flags(x11.conn, XCB_XKB_ID_USE_CORE_KBD,
                                         XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
                                         XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 0, 0, 0),
                &err)) ||
            err)
        {
            LOG(QFATAL) << "could not set up detectable autorepeat";
        }
    }

    if (mouseInput)
    {
        x11.extXi2 = xcb_get_extension_data(x11.conn, &xcb_input_id);
        if (!x11.extXi2 || !x11.extXi2->present)
        {
            LOG(QFATAL) << "could nolt set up XI2 extension";
        }

        struct
        {
            xcb_input_event_mask_t eventMask;
            uint32_t maskBits;
        } mask;

        mask.eventMask.deviceid = XCB_INPUT_DEVICE_ALL_MASTER;
        mask.eventMask.mask_len = 1;
        mask.maskBits = XCB_INPUT_XI_EVENT_MASK_RAW_MOTION;

        if (xcb_request_check(x11.conn,
                              xcb_input_xi_select_events_checked(x11.conn, x11.screen->root, 1, &mask.eventMask)))
        {
            LOG(QFATAL) << "could not setup XI2 extension";
        }
    }
}

auto X11CreateWindow(uint32_t width, uint32_t height, bool overrideRedirect, xcb_event_mask_t eventMask) -> xcb_window_t
{
    const xcb_window_t window = xcb_generate_id(x11.conn);

    xcb_create_window(x11.conn, XCB_COPY_FROM_PARENT, window, x11.screen->root, 0, 0, width, height, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, x11.screen->root_visual,
                      XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, (uint32_t[]){overrideRedirect, eventMask});

    xcb_map_window(x11.conn, window);

    return window;
}

void X11Flush()
{
    xcb_flush(x11.conn);
}

auto X11InternAtom(xcb_connection_t *conn, std::string_view name, bool onlyIfExists) -> xcb_atom_t
{
    xcb_intern_atom_reply_t *reply =
        xcb_intern_atom_reply(conn, xcb_intern_atom(conn, onlyIfExists, name.size(), name.data()), nullptr);
    absl::Cleanup replyFreer = [reply] -> void {
        if (reply)
            free(reply);
    };

    if (!reply || !reply->atom)
    {
        LOG(FATAL) << "could not intern atom " << name;
    }

    return reply->atom;
}

void X11SendClientMessage32(xcb_window_t window, xcb_atom_t type, xcb_atom_t arg1, uint32_t arg2, uint32_t arg3,
                            uint32_t arg4)
{
    xcb_client_message_event_t event = {
        .response_type = XCB_CLIENT_MESSAGE,
        .format = 32,
        .window = window,
        .type = type,
        .data = {.data32 = {arg1, arg2, arg3, arg4}},
    };

    xcb_send_event(x11.conn, false, window, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char *>(&event));
}

void X11SendWmTakeFocus(xcb_window_t window, uint32_t time)
{
    X11SendClientMessage32(window, x11.atoms.wm_protocols, x11.atoms.wm_take_focus, time, 0, 0);
}

void X11SendWmDeleteWindow(xcb_window_t window)
{
    X11SendClientMessage32(window, x11.atoms.wm_protocols, x11.atoms.wm_delete_window, XCB_CURRENT_TIME, 0, 0);
}

void X11SendConfigureNotify(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
                            uint16_t height, uint16_t borderWidth)
{
    xcb_configure_notify_event_t event = {
        .response_type = XCB_CONFIGURE_NOTIFY,
        .window = window,
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .border_width = borderWidth,
    };

    xcb_send_event(x11.conn, false, window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, reinterpret_cast<const char *>(&event));
}

//

auto X11InitializeKeyResolver(X11KeyResolver &keyResolver) -> bool
{
    keyResolver.ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!keyResolver.ctx)
        return false;

    const int32_t deviceId = xkb_x11_get_core_keyboard_device_id(x11.conn);
    if (deviceId == -1)
        return false;

    keyResolver.keymap =
        xkb_x11_keymap_new_from_device(keyResolver.ctx, x11.conn, deviceId, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keyResolver.keymap)
        return false;

    return true;
}

void X11FreeKeyResolver(X11KeyResolver &keyResolver)
{
    xkb_keymap_unref(keyResolver.keymap);
    xkb_context_unref(keyResolver.ctx);
}

auto X11ResolveKeyCode(const X11KeyResolver &keyResolver, std::string_view keyname) -> xcb_keycode_t
{
    const xkb_keycode_t keycode = xkb_keymap_key_by_name(keyResolver.keymap, keyname.data());
    CHECK(xkb_keycode_is_legal_x11(keycode));
    return keycode;
}

} // namespace platform_x11_internal

} // namespace nyla