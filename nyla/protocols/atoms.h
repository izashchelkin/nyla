#pragma once

#include <string_view>

#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

xcb_atom_t InternAtom(xcb_connection_t* conn, std::string_view name,
                      bool only_if_exists = false);

struct Atoms {
  static inline xcb_atom_t any = XCB_ATOM_ANY;
  static inline xcb_atom_t arc = XCB_ATOM_ARC;
  static inline xcb_atom_t atom = XCB_ATOM_ATOM;
  static inline xcb_atom_t bitmap = XCB_ATOM_BITMAP;
  static inline xcb_atom_t cap_height = XCB_ATOM_CAP_HEIGHT;
  static inline xcb_atom_t cardinal = XCB_ATOM_CARDINAL;
  static inline xcb_atom_t colormap = XCB_ATOM_COLORMAP;
  static inline xcb_atom_t copyright = XCB_ATOM_COPYRIGHT;
  static inline xcb_atom_t cursor = XCB_ATOM_CURSOR;
  static inline xcb_atom_t cut_buffer0 = XCB_ATOM_CUT_BUFFER0;
  static inline xcb_atom_t cut_buffer1 = XCB_ATOM_CUT_BUFFER1;
  static inline xcb_atom_t cut_buffer2 = XCB_ATOM_CUT_BUFFER2;
  static inline xcb_atom_t cut_buffer3 = XCB_ATOM_CUT_BUFFER3;
  static inline xcb_atom_t cut_buffer4 = XCB_ATOM_CUT_BUFFER4;
  static inline xcb_atom_t cut_buffer5 = XCB_ATOM_CUT_BUFFER5;
  static inline xcb_atom_t cut_buffer6 = XCB_ATOM_CUT_BUFFER6;
  static inline xcb_atom_t cut_buffer7 = XCB_ATOM_CUT_BUFFER7;
  static inline xcb_atom_t drawable = XCB_ATOM_DRAWABLE;
  static inline xcb_atom_t end_space = XCB_ATOM_END_SPACE;
  static inline xcb_atom_t family_name = XCB_ATOM_FAMILY_NAME;
  static inline xcb_atom_t font = XCB_ATOM_FONT;
  static inline xcb_atom_t font_name = XCB_ATOM_FONT_NAME;
  static inline xcb_atom_t full_name = XCB_ATOM_FULL_NAME;
  static inline xcb_atom_t integer = XCB_ATOM_INTEGER;
  static inline xcb_atom_t italic_angle = XCB_ATOM_ITALIC_ANGLE;
  static inline xcb_atom_t max_space = XCB_ATOM_MAX_SPACE;
  static inline xcb_atom_t min_space = XCB_ATOM_MIN_SPACE;
  static inline xcb_atom_t none = XCB_ATOM_NONE;
  static inline xcb_atom_t norm_space = XCB_ATOM_NORM_SPACE;
  static inline xcb_atom_t notice = XCB_ATOM_NOTICE;
  static inline xcb_atom_t pixmap = XCB_ATOM_PIXMAP;
  static inline xcb_atom_t point = XCB_ATOM_POINT;
  static inline xcb_atom_t point_size = XCB_ATOM_POINT_SIZE;
  static inline xcb_atom_t primary = XCB_ATOM_PRIMARY;
  static inline xcb_atom_t quad_width = XCB_ATOM_QUAD_WIDTH;
  static inline xcb_atom_t rectangle = XCB_ATOM_RECTANGLE;
  static inline xcb_atom_t resolution = XCB_ATOM_RESOLUTION;
  static inline xcb_atom_t resource_manager = XCB_ATOM_RESOURCE_MANAGER;
  static inline xcb_atom_t rgb_best_map = XCB_ATOM_RGB_BEST_MAP;
  static inline xcb_atom_t rgb_blue_map = XCB_ATOM_RGB_BLUE_MAP;
  static inline xcb_atom_t rgb_color_map = XCB_ATOM_RGB_COLOR_MAP;
  static inline xcb_atom_t rgb_default_map = XCB_ATOM_RGB_DEFAULT_MAP;
  static inline xcb_atom_t rgb_gray_map = XCB_ATOM_RGB_GRAY_MAP;
  static inline xcb_atom_t rgb_green_map = XCB_ATOM_RGB_GREEN_MAP;
  static inline xcb_atom_t rgb_red_map = XCB_ATOM_RGB_RED_MAP;
  static inline xcb_atom_t secondary = XCB_ATOM_SECONDARY;
  static inline xcb_atom_t strikeout_ascent = XCB_ATOM_STRIKEOUT_ASCENT;
  static inline xcb_atom_t strikeout_descent = XCB_ATOM_STRIKEOUT_DESCENT;
  static inline xcb_atom_t string = XCB_ATOM_STRING;
  static inline xcb_atom_t subscript_x = XCB_ATOM_SUBSCRIPT_X;
  static inline xcb_atom_t subscript_y = XCB_ATOM_SUBSCRIPT_Y;
  static inline xcb_atom_t superscript_x = XCB_ATOM_SUPERSCRIPT_X;
  static inline xcb_atom_t superscript_y = XCB_ATOM_SUPERSCRIPT_Y;
  static inline xcb_atom_t underline_position = XCB_ATOM_UNDERLINE_POSITION;
  static inline xcb_atom_t underline_thickness = XCB_ATOM_UNDERLINE_THICKNESS;
  static inline xcb_atom_t visualid = XCB_ATOM_VISUALID;
  static inline xcb_atom_t weight = XCB_ATOM_WEIGHT;
  static inline xcb_atom_t window = XCB_ATOM_WINDOW;
  static inline xcb_atom_t wm_class = XCB_ATOM_WM_CLASS;
  static inline xcb_atom_t wm_client_machine = XCB_ATOM_WM_CLIENT_MACHINE;
  static inline xcb_atom_t wm_command = XCB_ATOM_WM_COMMAND;
  static inline xcb_atom_t wm_hints = XCB_ATOM_WM_HINTS;
  static inline xcb_atom_t wm_icon_name = XCB_ATOM_WM_ICON_NAME;
  static inline xcb_atom_t wm_icon_size = XCB_ATOM_WM_ICON_SIZE;
  static inline xcb_atom_t wm_name = XCB_ATOM_WM_NAME;
  static inline xcb_atom_t wm_normal_hints = XCB_ATOM_WM_NORMAL_HINTS;
  static inline xcb_atom_t wm_size_hints = XCB_ATOM_WM_SIZE_HINTS;
  static inline xcb_atom_t wm_transient_for = XCB_ATOM_WM_TRANSIENT_FOR;
  static inline xcb_atom_t wm_zoom_hints = XCB_ATOM_WM_ZOOM_HINTS;
  static inline xcb_atom_t x_height = XCB_ATOM_X_HEIGHT;

  xcb_atom_t wm_delete_window;
  xcb_atom_t wm_protocols;
  xcb_atom_t wm_state;
  xcb_atom_t wm_take_focus;
};

Atoms InternAtoms(xcb_connection_t* conn);

}  // namespace nyla
