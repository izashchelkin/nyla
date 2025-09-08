#include <iostream>
#include <print>

#include "src/internal.hpp"

namespace nyla {

void handle_error(xcb_generic_error_t& error) {
    std::println(std::cerr, "Error!");
}

}    // namespace nyla
