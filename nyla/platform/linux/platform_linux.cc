#include "nyla/platform/linux/platform_linux.h"
#include "nyla/commons/align.h"
#include "nyla/commons/byteliterals.h"
#include "nyla/platform/platform.h"

#include "nyla/commons/assert.h"
#include "nyla/commons/cleanup.h"
#include "nyla/commons/log.h"
#include "nyla/commons/string.h"
#include "nyla/platform/platform.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xcb/xinput.h"
#include "xcb/xproto.h"
#include <array>
#include <cstdint>
#include <sys/mman.h>
#include <type_traits>
#include <unistd.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

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

auto main(int argc, char *argv[]) -> int
{
    nyla::PlatformMain();
    return 0;
}

namespace nyla
{

namespace
{

uint64_t g_PageSize;
char *g_AddressSpaceBase;
char *g_AddressSpaceAt;
uint64_t g_AddressSpaceSize;

int g_ScreenIndex{};
xcb_connection_t *g_Conn{};
xcb_screen_t *g_Screen{};
uint32_t g_ExtensionXInput2MajorOpCode{};

xcb_window_t g_Win{};
xcb_get_geometry_reply_t g_WinGeom{};

LinuxX11Platform::Atoms m_Atoms;

std::array<xcb_keycode_t, static_cast<uint32_t>(KeyPhysical::Count)> g_KeyPhysicalCodes;

} // namespace

auto LinuxX11Platform::WinGetHandle() -> xcb_window_t
{
    return g_Win;
}

auto LinuxX11Platform::GetScreenIndex() -> int
{
    return g_ScreenIndex;
}

auto LinuxX11Platform::GetScreen() -> xcb_screen_t *
{
    return g_Screen;
}

auto LinuxX11Platform::GetRoot() -> xcb_window_t
{
    return g_Screen->root;
}

auto LinuxX11Platform::GetConn() -> xcb_connection_t *
{
    return g_Conn;
}

auto LinuxX11Platform::GetAtoms() -> LinuxX11Platform::Atoms &
{
    return m_Atoms;
}

auto LinuxX11Platform::GetXInputExtensionMajorOpCode() -> uint32_t
{
    return g_ExtensionXInput2MajorOpCode;
}

void LinuxX11Platform::Grab()
{
    xcb_grab_server(g_Conn);
}

void LinuxX11Platform::Ungrab()
{
    xcb_ungrab_server(g_Conn);
}

void LinuxX11Platform::Flush()
{
    xcb_flush(g_Conn);
}

void LinuxX11Platform::SetWindow(xcb_window_t window)
{
    g_Win = window;
}

auto LinuxX11Platform::KeyPhysicalToKeyCode(KeyPhysical key) -> uint32_t
{
    return g_KeyPhysicalCodes[static_cast<uint32_t>(key)];
}

auto LinuxX11Platform::KeyCodeToKeyPhysical(uint32_t keyCode, KeyPhysical *outKeyPhysical) -> bool
{
    for (uint32_t i = 1; i < g_KeyPhysicalCodes.size(); ++i)
    {
        if (g_KeyPhysicalCodes[i] == keyCode)
        {
            *outKeyPhysical = static_cast<KeyPhysical>(i);
            return true;
        }
    }
    return false;
}

auto LinuxX11Platform::InternAtom(std::string_view name, bool onlyIfExists) -> xcb_atom_t
{
    xcb_intern_atom_reply_t *reply =
        xcb_intern_atom_reply(g_Conn, xcb_intern_atom(g_Conn, onlyIfExists, name.size(), name.data()), nullptr);
    if (!reply || !reply->atom)
        NYLA_LOG("could not intern atom " NYLA_SV_FMT, NYLA_SV_ARG(name));

    auto ret = reply->atom;
    free(reply);
    return ret;
}

void Platform::InitGraphical(const PlatformInitDesc &desc)
{
    g_Conn = xcb_connect(nullptr, &g_ScreenIndex);
    if (xcb_connection_has_error(g_Conn))
        NYLA_ASSERT(false && "could not connect to X server");

    g_Screen = xcb_aux_get_screen(g_Conn, g_ScreenIndex);
    NYLA_ASSERT(g_Screen);

#define X(name) m_Atoms.name = LinuxX11Platform::InternAtom(AsciiStrToUpper(#name), false);
    NYLA_X11_ATOMS(X)
#undef X

    if (Any(desc.enabledFeatures & PlatformFeature::KeyboardInput))
    {
        uint16_t majorXkbVersionOut, minorXkbVersionOut;
        uint8_t baseEventOut, baseErrorOut;

        if (!xkb_x11_setup_xkb_extension(g_Conn, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION,
                                         XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, &majorXkbVersionOut, &minorXkbVersionOut,
                                         &baseEventOut, &baseErrorOut))
            NYLA_ASSERT(false && "could not set up xkb extension");

        if (majorXkbVersionOut < XKB_X11_MIN_MAJOR_XKB_VERSION ||
            (majorXkbVersionOut == XKB_X11_MIN_MAJOR_XKB_VERSION && minorXkbVersionOut < XKB_X11_MIN_MINOR_XKB_VERSION))
            NYLA_ASSERT(false && "could not set up xkb extension");

        xcb_generic_error_t *err = nullptr;
        xcb_xkb_per_client_flags_reply_t *reply = xcb_xkb_per_client_flags_reply(
            g_Conn,
            xcb_xkb_per_client_flags(g_Conn, XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
                                     XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 0, 0, 0),
            &err);
        if (!reply || err)
            NYLA_ASSERT(false && "could not set up detectable autorepeat");

        {
            xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
            NYLA_ASSERT(ctx);

            const int32_t deviceId = xkb_x11_get_core_keyboard_device_id(g_Conn);
            NYLA_ASSERT(deviceId != -1);

            xkb_keymap *keymap = xkb_x11_keymap_new_from_device(ctx, g_Conn, deviceId, XKB_KEYMAP_COMPILE_NO_FLAGS);
            NYLA_ASSERT(keymap);

            for (uint32_t i = 1; i < static_cast<uint32_t>(KeyPhysical::Count); ++i)
            {
                const auto key = static_cast<KeyPhysical>(i);
                const char *xkbName = LinuxX11Platform::ConvertKeyPhysicalIntoXkbName(key);
                const xkb_keycode_t keycode = xkb_keymap_key_by_name(keymap, xkbName);
                NYLA_ASSERT(xkb_keycode_is_legal_x11(keycode));
                g_KeyPhysicalCodes[i] = keycode;
            }

            xkb_keymap_unref(keymap);
            xkb_context_unref(ctx);
        }
    }

    if (Any(desc.enabledFeatures & PlatformFeature::MouseInput))
    {
        const xcb_query_extension_reply_t *ext = xcb_get_extension_data(g_Conn, &xcb_input_id);
        if (!ext || !ext->present)
            NYLA_ASSERT(false && "could nolt set up XI2 extension");

        struct
        {
            xcb_input_event_mask_t eventMask;
            uint32_t maskBits;
        } mask;

        mask.eventMask.deviceid = XCB_INPUT_DEVICE_ALL_MASTER;
        mask.eventMask.mask_len = 2;
        mask.maskBits = XCB_INPUT_XI_EVENT_MASK_RAW_MOTION | XCB_INPUT_XI_EVENT_MASK_RAW_BUTTON_PRESS;

        if (xcb_request_check(g_Conn, xcb_input_xi_select_events_checked(g_Conn, g_Screen->root, 1, &mask.eventMask)))
            NYLA_ASSERT(false && "could not setup XI2 extension");

        g_ExtensionXInput2MajorOpCode = ext->major_opcode;
    }

    //

    g_PageSize = sysconf(_SC_PAGESIZE);
    if (!g_PageSize)
        g_PageSize = 4096;

    g_AddressSpaceSize = AlignedUp<uint64_t>(16_GiB, g_PageSize);
    g_AddressSpaceBase = (char *)mmap(nullptr, g_AddressSpaceSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    NYLA_ASSERT(g_AddressSpaceBase != MAP_FAILED);

    g_AddressSpaceAt = g_AddressSpaceBase;

    if (desc.open)
        WinOpen();
}

void LinuxX11Platform::SendConfigureNotify(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y,
                                           uint16_t width, uint16_t height, uint16_t borderWidth)
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

    xcb_send_event(g_Conn, false, window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, reinterpret_cast<const char *>(&event));
}

void LinuxX11Platform::SendClientMessage32(xcb_window_t window, xcb_atom_t type, xcb_atom_t arg1, uint32_t arg2,
                                           uint32_t arg3, uint32_t arg4)
{
    const xcb_client_message_event_t event = {
        .response_type = XCB_CLIENT_MESSAGE,
        .format = 32,
        .window = window,
        .type = type,
        .data = {.data32 = {arg1, arg2, arg3, arg4}},
    };

    xcb_send_event(g_Conn, false, window, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char *>(&event));
}

void LinuxX11Platform::SendWmTakeFocus(xcb_window_t window, uint32_t time)
{
    SendClientMessage32(window, m_Atoms.wm_protocols, m_Atoms.wm_take_focus, time, 0, 0);
}

void LinuxX11Platform::SendWmDeleteWindow(xcb_window_t window)
{
    SendClientMessage32(window, m_Atoms.wm_protocols, m_Atoms.wm_delete_window, XCB_CURRENT_TIME, 0, 0);
}

void Platform::WinOpen()
{
    if (g_Win)
    {
        xcb_map_window(g_Conn, g_Win);
        return;
    }

    constexpr auto kEventMask = static_cast<xcb_event_mask_t>(
        XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_BUTTON_PRESS |
        XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY);
    g_Win = LinuxX11Platform::CreateWin(g_Screen->width_in_pixels, g_Screen->height_in_pixels, false, kEventMask);

    xcb_get_geometry_reply_t *windowGeometry = xcb_get_geometry_reply(g_Conn, xcb_get_geometry(g_Conn, g_Win), nullptr);
    g_WinGeom = *windowGeometry;
    free(windowGeometry);
}

auto LinuxX11Platform::CreateWin(uint32_t widPlatformth, uint32_t height, bool overrideRedirect,
                                 xcb_event_mask_t eventMask) -> xcb_window_t
{
    const xcb_window_t window = xcb_generate_id(g_Conn);

    const std::array<uint32_t, 2> values{overrideRedirect, eventMask};
    xcb_create_window(g_Conn, XCB_COPY_FROM_PARENT, window, g_Screen->root, 0, 0, width, height, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, g_Screen->root_visual,
                      XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, values.data());

    xcb_map_window(g_Conn, window);
    xcb_flush(g_Conn);

    return window;
}

auto Platform::WinGetSize() -> PlatformWindowSize
{
    return PlatformWindowSize{
        .width = g_WinGeom.width,
        .height = g_WinGeom.height,
    };
}

auto Platform::WinPollEvent(PlatformEvent &outEvent) -> bool
{
    for (;;)
    {
        if (xcb_connection_has_error(g_Conn))
        {
            outEvent = {.type = PlatformEventType::Quit};
            return true;
        }

        xcb_generic_event_t *event = xcb_poll_for_event(g_Conn);
        if (!event)
            return false;

        Cleanup eventFreer([=]() -> void { free(event); });
        const uint8_t eventType = event->response_type & 0x7F;

        switch (eventType)
        {
        default:
            break;

        case XCB_KEY_PRESS: {
            auto keypress = reinterpret_cast<xcb_key_press_event_t *>(event);

            for (uint32_t i = 1; i < static_cast<uint32_t>(KeyPhysical::Count); ++i)
            {
                if (g_KeyPhysicalCodes[i] == keypress->detail)
                {
                    outEvent = PlatformEvent{
                        .type = PlatformEventType::KeyDown,
                        .key = static_cast<KeyPhysical>(i),
                    };
                    return true;
                }
            }

            continue;
        }

        case XCB_KEY_RELEASE: {
            auto keyrelease = reinterpret_cast<xcb_key_release_event_t *>(event);

            for (uint32_t i = 1; i < static_cast<uint32_t>(KeyPhysical::Count); ++i)
            {
                if (g_KeyPhysicalCodes[i] == keyrelease->detail)
                {
                    outEvent = PlatformEvent{
                        .type = PlatformEventType::KeyUp,
                        .key = static_cast<KeyPhysical>(i),
                    };
                    return true;
                }
            }

            continue;
        }

        case XCB_BUTTON_PRESS: {
            auto buttonpress = reinterpret_cast<xcb_button_press_event_t *>(event);
            outEvent = PlatformEvent{
                .type = PlatformEventType::MousePress,
                .mouse = {.code = buttonpress->detail},
            };
            return true;
        }

        case XCB_BUTTON_RELEASE: {
            auto buttonrelease = reinterpret_cast<xcb_button_release_event_t *>(event);
            outEvent = PlatformEvent{
                .type = PlatformEventType::MouseRelease,
                .mouse = {.code = buttonrelease->detail},
            };
            return true;
        }

        case XCB_CONFIGURE_NOTIFY: {
            auto configurenotify = reinterpret_cast<xcb_configure_notify_event_t *>(event);

            xcb_get_geometry_reply_t *newGeom =
                xcb_get_geometry_reply(g_Conn, xcb_get_geometry(g_Conn, g_Win), nullptr);

            const bool windowResized = newGeom->width != g_WinGeom.width || newGeom->height != g_WinGeom.height;
            g_WinGeom = *newGeom;
            free(newGeom);

            if (windowResized)
            {
                outEvent = PlatformEvent{.type = PlatformEventType::WinResize};
                return true;
            }

            continue;
        }

        case XCB_EXPOSE: {
            outEvent = PlatformEvent{.type = PlatformEventType::Repaint};
            return true;
        }

        case XCB_CLIENT_MESSAGE: {
            auto clientmessage = reinterpret_cast<xcb_client_message_event_t *>(event);

            if (clientmessage->format == 32 && clientmessage->type == m_Atoms.wm_protocols &&
                clientmessage->data.data32[0] == m_Atoms.wm_delete_window)
            {
                outEvent = {.type = PlatformEventType::Quit};
                return true;
            }

            break;
        }
        }
    }
}

auto LinuxX11Platform::ConvertKeyPhysicalIntoXkbName(KeyPhysical key) -> const char *
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

    default:
        return nullptr;
    }
}

} // namespace nyla