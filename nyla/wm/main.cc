#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_util.h>
#include <xcb/xproto.h>

#include <cstdint>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "nyla/keyboard/keyboard.h"
#include "nyla/layout/layout.h"
#include "nyla/spawn/spawn.h"
#include "nyla/wm/client.h"
#include "nyla/wm/utils.h"

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

  uint16_t modifier = XCB_MOD_MASK_4;

  std::vector<Keybind> keybinds;
  keybinds.reserve(3);

  keybinds.emplace_back("AD01", [&is_running] { is_running = false; });
  keybinds.emplace_back("AD02", [&layout_type] {
    switch (layout_type) {
      case LayoutType::kColumns:
        layout_type = LayoutType::kRows;
        break;
      case LayoutType::kRows:
        layout_type = LayoutType::kGrid;
        break;
      case LayoutType::kGrid:
        layout_type = LayoutType::kColumns;
        break;
    }
  });
  keybinds.emplace_back("AD05", [] {
    static const char* const kTermCmd[] = {"ghostty", nullptr};
    Spawn(kTermCmd);
  });

  if (!BindKeyboard(conn, screen->root, modifier, keybinds))
    LOG(QFATAL) << "could not bind keyboard";
  LOG(INFO) << "bind keyboard successful";

  ClientStack client_stack;

  while (is_running) {
    std::vector<Rect>&& layout =
        ComputeLayout(AsRect(screen), client_stack.size(), layout_type);
    client_stack.ApplyLayoutChanges(conn, layout);

    xcb_flush(conn);
    xcb_generic_event_t* event = xcb_wait_for_event(conn);
    if (!event) {
      LOG(ERROR) << "lost connection to X server";
      break;
    }

    do {
      switch (event->response_type & ~0x80) {
        case XCB_KEY_PRESS: {
          auto keypress = reinterpret_cast<xcb_key_press_event_t*>(event);
          if (keypress->state == modifier)
            HandleKeyPress(keypress->detail, keybinds);
          break;
        }
        case XCB_MAP_REQUEST: {
          client_stack.HandleMapRequest(
              conn, reinterpret_cast<xcb_map_request_event_t*>(event)->window);
          break;
        }
        case XCB_UNMAP_NOTIFY: {
          client_stack.HandleUnmapNotify(
              reinterpret_cast<xcb_unmap_notify_event_t*>(event)->window);
          break;
        }
        case 0: {
          LOG(FATAL) << "xcb error";
        }
      }

      event = xcb_poll_for_event(conn);
    } while (event && is_running);
  }

  return 0;
}

}  // namespace nyla

int main(int argc, char** argv) { return nyla::Main(argc, argv); }
