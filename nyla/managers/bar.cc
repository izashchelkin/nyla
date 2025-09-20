#include "nyla/managers/bar.h"

#include <cstdint>
#include <cstring>
#include <string_view>

#include "absl/log/log.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

bool Bar::Init(xcb_connection_t *conn, xcb_screen_t &screen) {
  window_ = xcb_generate_id(conn);

  if (xcb_request_check(
          conn, xcb_create_window_checked(
                    conn, XCB_COPY_FROM_PARENT, window_, screen.root, 0, 0,
                    screen.width_in_pixels, height_, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, screen.root_visual,
                    XCB_CW_BACK_PIXEL, (uint32_t[]){screen.black_pixel}))) {
    LOG(ERROR) << "could not create bar window";
    return false;
  }

  xcb_map_window(conn, window_);

  constexpr const char *kFontName = "fixed";

  xcb_font_t font = xcb_generate_id(conn);
  if (xcb_request_check(conn, xcb_open_font_checked(
                                  conn, font, strlen(kFontName), kFontName))) {
    LOG(ERROR) << "could not create bar font";
    return false;
  }

  gc_ = xcb_generate_id(conn);
  if (xcb_request_check(
          conn,
          xcb_create_gc_checked(
              conn, gc_, window_,
              XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT,
              (uint32_t[]){screen.white_pixel, screen.black_pixel, font}))) {
    LOG(ERROR) << "could not create bar GC";
    return false;
  }

  return true;
}

void Bar::Update(xcb_connection_t *conn, xcb_screen_t &screen,
                 std::string_view msg) {
  xcb_clear_area(conn, 0, window_, 0, 0, screen.width_in_pixels, height_);
  xcb_image_text_8(conn, msg.size(), window_, gc_, 8, 16, msg.data());
}

}  // namespace nyla
