#include <xcb/xcb.h>
#include <iostream>
#include <print>

#include "src/internal.hpp"

namespace nyla {

void handle_map_request(xcb_connection_t* conn,
                        xcb_map_request_event_t& event) {
	std::println(std::cerr, "MAP REQUEST");
    xcb_map_window(conn, event.window);
}

}    // namespace nyla
