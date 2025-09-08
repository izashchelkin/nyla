#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <cstdlib>

#include "src/internal.hpp"

namespace nyla {

void init_keyboard(xcb_connection_t* conn) {
    constexpr uint16_t major = XKB_X11_MIN_MAJOR_XKB_VERSION;
    constexpr uint16_t minor = XKB_X11_MIN_MINOR_XKB_VERSION;

    uint16_t major_xkb_version_out = 0;
    uint16_t minor_xkb_version_out = 0;
    uint8_t base_event_out = 0;
    uint8_t base_error_out = 0;

    if (!xkb_x11_setup_xkb_extension(
            conn, major, minor, XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
            &major_xkb_version_out, &minor_xkb_version_out, &base_event_out,
            &base_error_out)) {
        std::abort();
    }

    if (major_xkb_version_out < major ||
        (major_xkb_version_out == major && minor_xkb_version_out < minor)) {
        std::abort();
    }
}

void grab_keys(xcb_connection_t* conn, xcb_window_t root_window,
               KeyboardState& keyboard_state) {
    if (!keyboard_state.modifier) std::abort();

    xkb_context* ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx) std::abort();

    int32_t device_id = xkb_x11_get_core_keyboard_device_id(conn);
    if (device_id == -1) std::abort();

    xkb_keymap* keymap = xkb_x11_keymap_new_from_device(
        ctx, conn, device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);

    xcb_grab_server(conn);
    xcb_ungrab_key(conn, XCB_GRAB_ANY, root_window, XCB_MOD_MASK_ANY);

    for (const auto& [keyname, action] : keyboard_state.config) {
        const auto keycode = [&]() -> xcb_keycode_t {
            xkb_keycode_t keycode = xkb_keymap_key_by_name(keymap, keyname);
            if (keycode == XKB_KEYCODE_INVALID ||
                !xkb_keycode_is_legal_x11(keycode))
                std::abort();

            return static_cast<xcb_keycode_t>(keycode);
        }();

        if (auto [_, ok] = keyboard_state.keybinds.try_emplace(keycode, action);
            !ok)
            std::abort();

        auto grab = [&](uint16_t ignore_permutation) {
            if (ignore_permutation & keyboard_state.modifier) std::abort();
            xcb_grab_key(conn, 0, root_window,
                         keyboard_state.modifier | ignore_permutation, keycode,
                         XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        };
        grab(0);
        // TODO: locks
    }

    xkb_context_unref(ctx);
    xkb_keymap_unref(keymap);

    xcb_flush(conn);
    xcb_ungrab_server(conn);
}

}    // namespace nyla
