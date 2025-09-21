#include <sys/poll.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdlib>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "nyla/commons/rect.h"
#include "nyla/commons/spawn.h"
#include "nyla/commons/timer.h"
#include "nyla/layout/layout.h"
#include "nyla/protocols/atoms.h"
#include "nyla/protocols/wm_protocols.h"
#include "nyla/wm/bar_manager.h"
#include "nyla/wm/client_manager.h"
#include "nyla/wm/keyboard.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xcb/xproto.h"

namespace nyla {

static void ProcessXEvents(WMState& wm_state, const bool& is_running,
                           uint16_t modifier,
                           std::span<const Keybind> keybinds);

int Main(int argc, char** argv) {
  bool is_running = true;

  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  int iscreen;
  xcb_connection_t* conn = xcb_connect(nullptr, &iscreen);
  if (xcb_connection_has_error(conn)) {
    LOG(QFATAL) << "could not connect to X server";
  }
  absl::Cleanup disconnecter = [conn] { xcb_disconnect(conn); };

  xcb_screen_t screen = *xcb_aux_get_screen(conn, iscreen);

  if (xcb_request_check(
          conn, xcb_change_window_attributes_checked(
                    conn, screen.root, XCB_CW_EVENT_MASK,
                    (uint32_t[]){
                        XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                        XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                        XCB_EVENT_MASK_BUTTON_PRESS |
                        XCB_EVENT_MASK_BUTTON_RELEASE | XCB_FOCUS_IN |
                        XCB_FOCUS_OUT | XCB_EVENT_MASK_POINTER_MOTION}))) {
    LOG(QFATAL) << "another wm is already running";
  }

  if (!InitKeyboard(conn)) LOG(QFATAL) << "could not init keyboard";
  LOG(INFO) << "init keyboard successful";

  WMState wm_state{};
  wm_state.conn = conn;
  wm_state.screen = screen;
  wm_state.atoms = InternAtoms(conn);
  wm_state.stacks.resize(9);

  uint16_t modifier = XCB_MOD_MASK_4;
  std::vector<Keybind> keybinds;

  keybinds.emplace_back("AD01", [&is_running] { is_running = false; });
  keybinds.emplace_back("AD02", [&wm_state] { NextLayout(wm_state); });
  keybinds.emplace_back("AD03", [&wm_state] { PrevStack(wm_state); });
  keybinds.emplace_back("AD04", [&wm_state] { NextStack(wm_state); });
  keybinds.emplace_back("AD05", [] { Spawn({{"ghostty", nullptr}}); });

  keybinds.emplace_back("AC02", [] { Spawn({{"dmenu_run", nullptr}}); });
  keybinds.emplace_back("AC03", [&wm_state] { MoveFocus(wm_state, -1); });
  keybinds.emplace_back("AC04", [&wm_state] { MoveFocus(wm_state, 1); });

  keybinds.emplace_back("AB02", [&wm_state] {
    CHECK_LT(wm_state.active_stack_idx, wm_state.stacks.size());
    const auto& stack = wm_state.stacks[wm_state.active_stack_idx];
    if (stack.active_client_window) {
      Send_WM_Delete_Window(wm_state.conn, stack.active_client_window,
                            wm_state.atoms);
    }
  });

  // keybinds.emplace_back("AB05", [conn, &client_manager, &atoms] {});

  if (!BindKeyboard(conn, screen.root, modifier, keybinds))
    LOG(QFATAL) << "could not bind keyboard";
  LOG(INFO) << "bind keyboard successful";

  BarManager bar_manager;
  if (!bar_manager.Init(conn, screen)) {
    LOG(QFATAL) << "could not init bar";
  }

  using namespace std::chrono_literals;
  int tfd = MakeTimerFd(501ms);

  if (!tfd) LOG(QFATAL) << "MakeTimerFdMillis";
  absl::Cleanup tfd_closer = [tfd] { close(tfd); };

  auto fds = std::to_array<pollfd>(
      {{.fd = xcb_get_file_descriptor(conn), .events = POLLIN},
       {.fd = tfd, .events = POLLIN}});

  while (is_running && !xcb_connection_has_error(conn)) {
    xcb_flush(conn);

    if (poll(fds.data(), fds.size(), -1) == -1) {
      PLOG(ERROR) << "poll";
      break;
    }

    if (fds[0].revents & POLLIN) {
      ProcessXEvents(wm_state, is_running, modifier, keybinds);

      CHECK_LT(wm_state.active_stack_idx, wm_state.stacks.size());
      const auto& stack = wm_state.stacks[wm_state.active_stack_idx];

      ApplyLayoutChanges(
          wm_state,
          ComputeLayout(ApplyMarginTop(Rect(screen.width_in_pixels,
                                            screen.height_in_pixels),
                                       bar_manager.height()),
                        stack.client_windows.size(), 2, stack.layout_type));
    }

    if (fds[1].revents & POLLIN) {
      uint64_t expirations;
      if (read(tfd, &expirations, sizeof(expirations)) >= 0)
        bar_manager.Update(conn, screen);  // TODO: also on Expose
    }
  }

  return 0;
}

static void ProcessXEvents(WMState& wm_state, const bool& is_running,
                           uint16_t modifier,
                           std::span<const Keybind> keybinds) {
  while (is_running) {
    xcb_generic_event_t* event = xcb_poll_for_event(wm_state.conn);
    if (!event) break;
    absl::Cleanup event_freer = [event] { free(event); };

    bool is_synthethic = event->response_type & 0x80;
    uint8_t event_type = event->response_type & 0x7F;

    if (is_synthethic && event_type != XCB_CLIENT_MESSAGE) {
      // continue;
    }

    switch (event_type) {
      case XCB_KEY_PRESS: {
        auto keypress = reinterpret_cast<xcb_key_press_event_t*>(event);
        if (keypress->state == modifier)
          HandleKeyPress(keypress->detail, keybinds);
        break;
      }
      case XCB_PROPERTY_NOTIFY: {
        // TODO:
        break;
      }
      case XCB_MAP_REQUEST: {
        xcb_map_window(
            wm_state.conn,
            reinterpret_cast<xcb_map_request_event_t*>(event)->window);

        ManageClient(
            wm_state,
            reinterpret_cast<xcb_unmap_notify_event_t*>(event)->window);
        break;
      }
      case XCB_MAP_NOTIFY: {
        break;
      }
      case XCB_UNMAP_NOTIFY: {
        UnmanageClient(
            wm_state,
            reinterpret_cast<xcb_unmap_notify_event_t*>(event)->window);
        break;
      }
      case XCB_DESTROY_NOTIFY: {
        UnmanageClient(
            wm_state,
            reinterpret_cast<xcb_destroy_notify_event_t*>(event)->window);
        break;
      }
      case XCB_ENTER_NOTIFY: {
        // auto enternotify =
        // reinterpret_cast<xcb_enter_notify_event_t*>(event); if
        // (enternotify->event != wm_state.screen.root)
        //   client_manager.FocusWindow(conn, screen, enternotify->event);
        break;
      }
      case XCB_FOCUS_IN:
      case XCB_FOCUS_OUT: {
        auto reply = xcb_get_input_focus_reply(
            wm_state.conn, xcb_get_input_focus(wm_state.conn), nullptr);
        xcb_window_t window = reply->focus;
        free(reply);

        if (!wm_state.clients.contains(window)) {
          for (;;) {
            xcb_query_tree_reply_t* reply = xcb_query_tree_reply(
                wm_state.conn, xcb_query_tree(wm_state.conn, window), nullptr);
            absl::Cleanup reply_freer = [reply] { free(reply); };
            if (reply->parent == reply->root) break;
          }
        }

        CHECK_LT(wm_state.active_stack_idx, wm_state.stacks.size());
        xcb_window_t active_client_window =
            wm_state.stacks[wm_state.active_stack_idx].active_client_window;

        LOG(INFO) << "focusin " << active_client_window << " =?= " << window;

        if (active_client_window != window) {
          if (active_client_window) {
            xcb_set_input_focus(wm_state.conn, XCB_INPUT_FOCUS_POINTER_ROOT,
                                active_client_window, XCB_CURRENT_TIME);
          } else {
            xcb_set_input_focus(wm_state.conn, XCB_INPUT_FOCUS_POINTER_ROOT,
                                wm_state.screen.root, XCB_CURRENT_TIME);
          }
        }

        break;
      }
      case 0: {
        LOG(FATAL) << "xcb error";
      }
    }
  }
}

}  // namespace nyla

int main(int argc, char** argv) { return nyla::Main(argc, argv); }
