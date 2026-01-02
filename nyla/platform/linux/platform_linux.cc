#include "nyla/platform/linux/platform_linux.h"
#include "nyla/platform/platform.h"

#include "nyla/commons/assert.h"
#include "nyla/commons/cleanup.h"
#include "nyla/commons/string.h"
#include "nyla/commons/log.h"
#include "nyla/platform/platform.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xcb/xinput.h"
#include <cstdint>
#include <array>
#include <xkbcommon/xkbcommon-x11.h>

#include "vulkan/vulkan_core.h"
#include "vulkan/vulkan_xcb.h"

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

int main(int argc, char *argv[])
{
    nyla::PlatformMain();
    return 0;
}

namespace nyla
{

auto Platform::Impl::InternAtom(std::string_view name, bool onlyIfExists) -> xcb_atom_t
{
    xcb_intern_atom_reply_t *reply =
        xcb_intern_atom_reply(m_Conn, xcb_intern_atom(m_Conn, onlyIfExists, name.size(), name.data()), nullptr);
    if (!reply || !reply->atom)
        NYLA_LOG("could not intern atom " NYLA_SV_FMT, NYLA_SV_ARG(name));

    auto ret = reply->atom;
    free(reply);
    return ret;
}

void Platform::Impl::Init(const PlatformInitDesc &desc)
{
    m_Conn = xcb_connect(nullptr, &m_ScreenIndex);
    if (xcb_connection_has_error(m_Conn))
        NYLA_ASSERT(false && "could not connect to X server");

    m_Screen = xcb_aux_get_screen(m_Conn, m_ScreenIndex);
    NYLA_ASSERT(m_Screen);

#define X(name) m_Atoms.name = InternAtom(AsciiStrToUpper(#name), false);
    NYLA_X11_ATOMS(X)
#undef X

    if (Any(desc.enabledFeatures & PlatformFeature::KeyboardInput))
    {
        uint16_t majorXkbVersionOut, minorXkbVersionOut;
        uint8_t baseEventOut, baseErrorOut;

        if (!xkb_x11_setup_xkb_extension(m_Conn, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION,
                                         XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, &majorXkbVersionOut, &minorXkbVersionOut,
                                         &baseEventOut, &baseErrorOut))
            NYLA_ASSERT(false && "could not set up xkb extension");

        if (majorXkbVersionOut < XKB_X11_MIN_MAJOR_XKB_VERSION ||
            (majorXkbVersionOut == XKB_X11_MIN_MAJOR_XKB_VERSION && minorXkbVersionOut < XKB_X11_MIN_MINOR_XKB_VERSION))
            NYLA_ASSERT(false && "could not set up xkb extension");

        xcb_generic_error_t *err = nullptr;
        xcb_xkb_per_client_flags_reply_t *reply = xcb_xkb_per_client_flags_reply(
            m_Conn,
            xcb_xkb_per_client_flags(m_Conn, XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
                                     XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 0, 0, 0),
            &err);
        if (!reply || err)
            NYLA_ASSERT(false && "could not set up detectable autorepeat");
    }
    

    if (Any(desc.enabledFeatures & PlatformFeature::MouseInput))
    {
        const xcb_query_extension_reply_t *ext = xcb_get_extension_data(m_Conn, &xcb_input_id);
        if (!ext || !ext->present)
            NYLA_ASSERT(false && "could nolt set up XI2 extension");

        struct
        {
            xcb_input_event_mask_t eventMask;
            uint32_t maskBits;
        } mask;

        mask.eventMask.deviceid = XCB_INPUT_DEVICE_ALL_MASTER;
        mask.eventMask.mask_len = 1;
        mask.maskBits = XCB_INPUT_XI_EVENT_MASK_RAW_MOTION;

        if (xcb_request_check(m_Conn, xcb_input_xi_select_events_checked(m_Conn, m_Screen->root, 1, &mask.eventMask)))
            NYLA_ASSERT(false && "could not setup XI2 extension");

        m_ExtensionXInput2MajorOpCode = ext->major_opcode;
    }
}

auto Platform::Impl::CreateWin() -> PlatformWindow
{
    xcb_window_t window =
        CreateWin(m_Screen->width_in_pixels, m_Screen->height_in_pixels, false,
                  static_cast<xcb_event_mask_t>(XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                                                XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE));
    return {.handle = static_cast<uint64_t>(window)};
}

auto Platform::Impl::CreateWin(uint32_t width, uint32_t height, bool overrideRedirect, xcb_event_mask_t eventMask)
    -> xcb_window_t
{
    const xcb_window_t window = xcb_generate_id(m_Conn);

    const std::array<uint32_t, 2> values{overrideRedirect, eventMask};
    xcb_create_window(m_Conn, XCB_COPY_FROM_PARENT, window, m_Screen->root, 0, 0, width, height, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, m_Screen->root_visual,
                      XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, values.data());

    xcb_map_window(m_Conn, window);
    xcb_flush(m_Conn);

    return window;
}

auto Platform::Impl::GetWindowSize(PlatformWindow platformWindow) -> PlatformWindowSize
{
    const auto window = static_cast<xcb_window_t>(platformWindow.handle);
    const xcb_get_geometry_reply_t *windowGeometry =
        xcb_get_geometry_reply(m_Conn, xcb_get_geometry(m_Conn, window), nullptr);
    return {
        .width = windowGeometry->width,
        .height = windowGeometry->height,
    };
}

auto Platform::Impl::PollEvent(PlatformEvent &outEvent) -> bool
{
    for (;;)
    {
        if (xcb_connection_has_error(m_Conn))
        {
            outEvent = {.type = PlatformEventType::ShouldExit};
            return true;
        }

        xcb_generic_event_t *event = xcb_poll_for_event(m_Conn);
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
            outEvent = {
                .type = PlatformEventType::KeyPress,
                .key = {.code = keypress->detail},
            };
            return true;
        }

        case XCB_KEY_RELEASE: {
            auto keyrelease = reinterpret_cast<xcb_key_release_event_t *>(event);
            outEvent = {
                .type = PlatformEventType::KeyRelease,
                .key = {.code = keyrelease->detail},
            };
            return true;
        }

        case XCB_BUTTON_PRESS: {
            auto buttonpress = reinterpret_cast<xcb_button_press_event_t *>(event);
            outEvent = {
                .type = PlatformEventType::MousePress,
                .key = {.code = buttonpress->detail},
            };
            return true;
        }

        case XCB_BUTTON_RELEASE: {
            auto buttonrelease = reinterpret_cast<xcb_button_release_event_t *>(event);
            outEvent = {
                .type = PlatformEventType::MouseRelease,
                .key = {.code = buttonrelease->detail},
            };
            return true;
        }

        case XCB_EXPOSE: {
            outEvent = {.type = PlatformEventType::ShouldRedraw};
            return true;
        }

        case XCB_CLIENT_MESSAGE: {
            auto clientmessage = reinterpret_cast<xcb_client_message_event_t *>(event);

            if (clientmessage->format == 32 && clientmessage->type == m_Atoms.wm_protocols &&
                clientmessage->data.data32[0] == m_Atoms.wm_delete_window)
            {
                outEvent = {.type = PlatformEventType::ShouldExit};
                return true;
            }

            break;
        }
        }
    }
}

//

void Platform::Init(const PlatformInitDesc &desc)
{
    NYLA_ASSERT(!m_Impl);
    m_Impl = new Impl();
    m_Impl->Init(desc);
}

auto Platform::CreateWin() -> PlatformWindow
{
    return m_Impl->CreateWin();
}

auto Platform::GetWindowSize(PlatformWindow window) -> PlatformWindowSize
{
    return m_Impl->GetWindowSize(window);
}

auto Platform::PollEvent(PlatformEvent &outEvent) -> bool
{
    return m_Impl->PollEvent(outEvent);
}

Platform *g_Platform = new Platform();

} // namespace nyla