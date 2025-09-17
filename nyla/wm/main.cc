#include <sys/poll.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdlib>

#include "absl/cleanup/cleanup.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "nyla/keyboard/keyboard.h"
#include "nyla/layout/layout.h"
#include "nyla/spawn/spawn.h"
#include "nyla/timer/timer.h"
#include "nyla/wm/bar_manager/bar_manager.h"
#include "nyla/wm/client.h"
#include "nyla/wm/utils.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xcb/xproto.h"

namespace nyla {

static void ProcessXEvents(xcb_connection_t* conn, const bool& is_running,
                           ClientStackManager& stack_manager, uint16_t modifier,
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
          conn,
          xcb_change_window_attributes_checked(
              conn, screen.root, XCB_CW_EVENT_MASK,
              (uint32_t[]){
                  XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                  XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                  XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                  XCB_EVENT_MASK_BUTTON_MOTION}))) {
    LOG(QFATAL) << "another wm is already running";
  }

  if (!InitKeyboard(conn)) LOG(QFATAL) << "could not init keyboard";
  LOG(INFO) << "init keyboard successful";

  ClientStackManager stack_manager;

  uint16_t modifier = XCB_MOD_MASK_4;
  std::vector<Keybind> keybinds;
  keybinds.reserve(3);

  keybinds.emplace_back("AD01", [&is_running] { is_running = false; });
  keybinds.emplace_back("AD02",
                        [&stack_manager] { stack_manager.NextLayout(); });
  keybinds.emplace_back("AD03",
                        [&stack_manager] { stack_manager.PrevStack(); });
  keybinds.emplace_back("AD04",
                        [&stack_manager] { stack_manager.NextStack(); });
  keybinds.emplace_back("AD05", [] {
    static const char* const kTermCmd[] = {"ghostty", nullptr};
    Spawn(kTermCmd);
  });

  keybinds.emplace_back("AC03", [conn, &stack_manager, &screen] {
    stack_manager.FocusPrev(conn, screen);
  });
  keybinds.emplace_back("AC04", [conn, &stack_manager, &screen] {
    stack_manager.FocusNext(conn, screen);
  });

  if (!BindKeyboard(conn, screen.root, modifier, keybinds))
    LOG(QFATAL) << "could not bind keyboard";
  LOG(INFO) << "bind keyboard successful";

  BarManager bar_manager;
  if (!bar_manager.Init(conn, screen)) {
    LOG(QFATAL) << "could not init bar";
  }

  using namespace std::chrono_literals;
  int tfd = MakeTimerFd(750ms);

  if (!tfd) LOG(QFATAL) << "MakeTimerFdMillis";
  absl::Cleanup tfd_closer = [tfd] { close(tfd); };

  auto fds = std::to_array<pollfd>(
      {{.fd = xcb_get_file_descriptor(conn), .events = POLLIN},
       {.fd = tfd, .events = POLLIN}});

  while (is_running && !xcb_connection_has_error(conn)) {
    std::vector<Rect>&& layout =
        ComputeLayout(AsRect(screen, bar_manager.height()),
                      stack_manager.size(), 2, stack_manager.layout_type());
    stack_manager.ApplyLayoutChanges(conn, screen, layout);
    xcb_flush(conn);

    if (poll(fds.data(), fds.size(), -1) == -1) {
      PLOG(ERROR) << "poll";
      break;
    }

    if (fds[0].revents & POLLIN) {
      ProcessXEvents(conn, is_running, stack_manager, modifier, keybinds);
    }

    if (fds[1].revents & POLLIN) {
      uint64_t expirations;
      if (read(tfd, &expirations, sizeof(expirations)) >= 0)
        bar_manager.Update(conn, screen);
    }
  }

  return 0;
}

static void ProcessXEvents(xcb_connection_t* conn, const bool& is_running,
                           ClientStackManager& stack_manager, uint16_t modifier,
                           std::span<const Keybind> keybinds) {
  while (is_running) {
    xcb_generic_event_t* event = xcb_poll_for_event(conn);
    if (!event) break;
    absl::Cleanup event_freer = [event] { free(event); };

    switch (event->response_type & ~0x80) {
      case XCB_KEY_PRESS: {
        auto keypress = reinterpret_cast<xcb_key_press_event_t*>(event);
        if (keypress->state == modifier)
          HandleKeyPress(keypress->detail, keybinds);
        break;
      }
      case XCB_MAP_REQUEST: {
        stack_manager.ManageClient(
            conn, reinterpret_cast<xcb_map_request_event_t*>(event)->window);
        break;
      }
      case XCB_UNMAP_NOTIFY: {
        stack_manager.UnmanageClient(
            reinterpret_cast<xcb_unmap_notify_event_t*>(event)->window);
        break;
      }
      case XCB_DESTROY_NOTIFY: {
        LOG(INFO) << "DestroyNotify";
        stack_manager.UnmanageClient(
            reinterpret_cast<xcb_destroy_notify_event_t*>(event)->window);
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
