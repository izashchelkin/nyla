#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_util.h>
#include <xcb/xproto.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "nyla/keyboard/keyboard.h"
#include "nyla/layout/layout.h"
#include "nyla/spawn/spawn.h"
#include "nyla/wm/client.h"

namespace nyla {

int Main(int argc, char** argv) {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  int iscreen;
  xcb_connection_t* conn = xcb_connect(nullptr, &iscreen);
  if (xcb_connection_has_error(conn)) {
    LOG(QFATAL) << "could not connect to X server";
  }

  xcb_screen_t* screen = xcb_aux_get_screen(conn, iscreen);

  if (xcb_request_check(
          conn,
          xcb_change_window_attributes_checked(
              conn, screen->root, XCB_CW_EVENT_MASK,
              (uint32_t[]){
                  XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                  XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                  XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                  XCB_EVENT_MASK_BUTTON_MOTION}))) {
    LOG(QFATAL) << "another wm is already running";
  }

  if (!InitKeyboard(conn)) LOG(QFATAL) << "could not init keyboard";
  LOG(INFO) << "init keyboard successful";

  bool is_running = true;
  LayoutType layout_type = LayoutType::kColumns;

  auto modifier = XCB_MOD_MASK_4;
  auto keybinds = std::to_array<KeyBind>({
      {.keyname = "AD01", .action = [&]() { is_running = false; }},
      {.keyname = "AD02",
       .action =
           [&]() {
             layout_type = layout_type == LayoutType::kColumns
                               ? LayoutType::kRows
                               : LayoutType::kColumns;
           }},
      {.keyname = "AD05",
       .action =
           []() {
             static const char* term_cmd[] = {"ghostty", nullptr};
             if (Spawn({term_cmd, 2}))
               LOG(INFO) << "spawn successful";
             else
               LOG(ERROR) << "spawn failed";
           }},
  });
  if (!BindKeyboard(conn, screen->root, modifier, keybinds))
    LOG(QFATAL) << "could not bind keyboard";
  LOG(INFO) << "bind keyboard successful";

  std::vector<Client> clients;

  while (is_running) {
    std::vector<Rect> new_rects = LayoutComputeRectPlacements(
        {.width = screen->width_in_pixels, .height = screen->height_in_pixels},
        clients.size(), layout_type);

    for (size_t i = 0; i < clients.size(); ++i) {
      if (new_rects[i] != clients[i].rect) {
        LOG(INFO) << "configured " << clients[i].window << " to "
                  << new_rects[i];

        xcb_configure_window(
            conn, clients[i].window,
            XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
            (uint32_t[]){new_rects[i].x, new_rects[i].y, new_rects[i].width,
                         new_rects[i].height});
      }
    }

    xcb_flush(conn);
    xcb_generic_event_t* event = xcb_wait_for_event(conn);
    if (!event) break;

    do {
      uint8_t event_type = event->response_type & ~0x80;
      switch (event_type) {
        case 0: {
          LOG(FATAL) << "XCB Error!";
        }

        case XCB_MAP_REQUEST: {
          auto map_request = reinterpret_cast<xcb_map_request_event_t*>(event);
          xcb_map_window(conn, map_request->window);

          LOG(INFO) << "map request from " << map_request->window;

          if (!std::any_of(clients.cbegin(), clients.cend(),
                          [map_request](auto client) {
                            return client.window == map_request->window;
                          })) {
            clients.emplace_back(Client{.window = map_request->window});
          }

          break;
        }

        case XCB_UNMAP_NOTIFY: {
          auto unmap_notify =
              reinterpret_cast<xcb_unmap_notify_event_t*>(event);
          for (auto it = clients.begin(); it != clients.end(); ++it) {
            if (it->window == unmap_notify->window) {
              it = clients.erase(it);
              break;
            }
          }
          break;
        }

        case XCB_CONFIGURE_REQUEST: {
          break;
        }

        case XCB_KEY_PRESS: {
          auto keypress = reinterpret_cast<xcb_key_press_event_t*>(event);
          if (keypress->state == modifier)
            HandleKeyPress(keypress->detail, keybinds);
          break;
        }
      }

      event = xcb_poll_for_event(conn);
    } while (event && is_running);
  }

  return 0;
}

}  // namespace nyla

int main(int argc, char** argv) { return nyla::Main(argc, argv); }
