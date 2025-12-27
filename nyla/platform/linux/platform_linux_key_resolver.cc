#include "absl/log/check.h"
#include "nyla/platform/platform_key_resolver.h"
#include "xcb/xcb.h"
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

namespace nyla
{

struct PlatformKeyResolverState
{
    xcb_connection_t *conn;
    xkb_context *ctx;
    xkb_keymap *keymap;
};

void PlatformKeyResolver::Init()
{
    CHECK(!m_State);
    m_State = new PlatformKeyResolverState{};

    auto &conn = m_State->conn;
    auto &ctx = m_State->ctx;
    auto &keymap = m_State->keymap;

    conn = xcb_connect(nullptr, nullptr);
    CHECK(!xcb_connection_has_error(conn));

    ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    CHECK(ctx);

    const int32_t deviceId = xkb_x11_get_core_keyboard_device_id(conn);
    CHECK_NE(deviceId, -1);

    keymap = xkb_x11_keymap_new_from_device(ctx, conn, deviceId, XKB_KEYMAP_COMPILE_NO_FLAGS);
    CHECK(keymap);
}

void PlatformKeyResolver::Destroy()
{
    if (m_State->keymap)
        xkb_keymap_unref(m_State->keymap);
    if (m_State->ctx)
        xkb_context_unref(m_State->ctx);
    if (m_State->conn)
        xcb_disconnect(m_State->conn);

    free(m_State);
    m_State = nullptr;
}

namespace
{
auto ConvertKeyPhysicalIntoXkbName(KeyPhysical key) -> const char *;
}

auto PlatformKeyResolver::ResolveKeyCode(KeyPhysical key) -> uint32_t
{
    const char *xkbKeyname = ConvertKeyPhysicalIntoXkbName(key);
    const xkb_keycode_t keycode = xkb_keymap_key_by_name(m_State->keymap, xkbKeyname);
    CHECK(xkb_keycode_is_legal_x11(keycode));
    return keycode;
}

namespace
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

} // namespace

} // namespace nyla
