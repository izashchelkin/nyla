#include <xcb/xcb.h>

#include <iostream>
#include <print>
#include <vector>

#include "src/internal.hpp"

namespace nyla {

void handle_configure_request(xcb_connection_t* conn,
                              xcb_configure_request_event_t& event) {
    uint16_t value_mask = event.value_mask;
    std::vector<uint32_t> values;

    if (value_mask & XCB_CONFIG_WINDOW_X) values.emplace_back(event.x);
    if (value_mask & XCB_CONFIG_WINDOW_Y) values.emplace_back(event.y);
    if (value_mask & XCB_CONFIG_WINDOW_WIDTH) values.emplace_back(event.width);
    if (value_mask & XCB_CONFIG_WINDOW_HEIGHT)
        values.emplace_back(event.height);
    if (value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
        values.emplace_back(event.border_width);
    if (value_mask & XCB_CONFIG_WINDOW_SIBLING)
        values.emplace_back(event.sibling);
    if (value_mask & XCB_CONFIG_WINDOW_STACK_MODE)
        values.emplace_back(event.stack_mode);

    std::println(std::cerr, "CONFIGURING");
    xcb_configure_window(conn, event.window, value_mask, values.data());
}

}    // namespace nyla
