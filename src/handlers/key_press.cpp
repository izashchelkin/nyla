#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "src/internal.hpp"

namespace nyla {

void handle_key_press(xcb_connection_t* conn, KeyboardState keyboard_state,
                      bool* running, xcb_key_press_event_t& event) {
    auto& keycode = event.detail;
    if ((keyboard_state.modifier & event.state) != keyboard_state.modifier ||
        !keyboard_state.keybinds.contains(keycode))
        return;

    switch (keyboard_state.keybinds.at(keycode)) {
        case Action::ExitWM: {
            *running = false;
            break;
        }
        case Action::SpawnTerminal: {
            spawn({"ghostty", nullptr});
            break;
        }
    }
}

}    // namespace nyla
