#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/xproto.h>

#include <cstdint>
#include <iostream>
#include <print>

#include "src/internal.hpp"

namespace nyla {

int main() {
    int nscreen;
    xcb_connection_t* conn = xcb_connect(nullptr, &nscreen);
    if (xcb_connection_has_error(conn)) {
        std::println(std::cerr, "could not connect to X server");
        return 1;
    }

    xcb_screen_t* screen = xcb_aux_get_screen(conn, nscreen);
    if (!screen) {
        std::println(std::cerr, "screen is null");
        return 1;
    }

    if (xcb_request_check(
            conn, xcb_change_window_attributes_checked(
                      conn, screen->root, XCB_CW_EVENT_MASK,
                      (uint32_t[]){XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                                   XCB_EVENT_MASK_KEY_PRESS |
                                   XCB_EVENT_MASK_KEY_RELEASE |
                                   XCB_EVENT_MASK_BUTTON_PRESS |
                                   XCB_EVENT_MASK_BUTTON_RELEASE |
                                   XCB_EVENT_MASK_BUTTON_MOTION}))) {
        std::println(std::cerr, "another wm is already running");
        return 1;
    }

    KeyboardState keyboard_state{};
    keyboard_state.modifier = XCB_MOD_MASK_4;
    keyboard_state.config = {
        {"AD01", Action::ExitWM},
        {"AD05", Action::SpawnTerminal},
    };

    init_keyboard(conn);
    grab_keys(conn, screen->root, keyboard_state);

    bool running = true;
    event_loop(conn, &running, keyboard_state);

    xcb_disconnect(conn);
    return 0;
}

}    // namespace nyla
