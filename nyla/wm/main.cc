#include <sys/poll.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <variant>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "nyla/commons/spawn.h"
#include "nyla/commons/timerfd.h"
#include "nyla/fs/nylafs.h"
#include "nyla/wm/window_manager.h"
#include "nyla/x11/send.h"
#include "nyla/x11/wm_protocols.h"
#include "nyla/x11/x11.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

int Main(int argc, char** argv) {
  bool is_running = true;

  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  InitializeX11();

  xcb_grab_server(x11.conn);

  if (xcb_request_check(
          x11.conn,
          xcb_change_window_attributes_checked(
              x11.conn, x11.screen->root, XCB_CW_EVENT_MASK,
              (uint32_t[]){
                  XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                  XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                  XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                  XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                  XCB_EVENT_MASK_FOCUS_CHANGE |
                  XCB_EVENT_MASK_POINTER_MOTION}))) {
    LOG(QFATAL) << "another wm is already running";
  }

  InitializeWM();

  uint16_t modifier = XCB_MOD_MASK_4;
  std::vector<
      std::pair<xcb_keycode_t,
                std::variant<void (*)(xcb_timestamp_t timestamp), void (*)()>>>
      keybinds;

  {
    KeyResolver key_resolver;
    InitializeKeyResolver(key_resolver);

    // W
    keybinds.emplace_back(ResolveKeyCode(key_resolver, "AD02"), NextLayout);
    // E
    keybinds.emplace_back(ResolveKeyCode(key_resolver, "AD03"), MoveStackPrev);
    // R
    keybinds.emplace_back(ResolveKeyCode(key_resolver, "AD04"), MoveStackNext);
    // D
    keybinds.emplace_back(ResolveKeyCode(key_resolver, "AC03"), MoveLocalPrev);
    // F
    keybinds.emplace_back(ResolveKeyCode(key_resolver, "AC04"), MoveLocalNext);
    // G
    keybinds.emplace_back(ResolveKeyCode(key_resolver, "AC05"), ToggleZoom);
    // X
    keybinds.emplace_back(ResolveKeyCode(key_resolver, "AB02"), CloseActive);
    // V
    keybinds.emplace_back(ResolveKeyCode(key_resolver, "AB04"), ToggleFollow);
    // T
    keybinds.emplace_back(
        ResolveKeyCode(key_resolver, "AD05"),
        [](xcb_timestamp_t time) { Spawn({{"ghostty", nullptr}}); });
    // S
    keybinds.emplace_back(
        ResolveKeyCode(key_resolver, "AC02"),
        [](xcb_timestamp_t time) { Spawn({{"dmenu_run", nullptr}}); });

    FreeKeyResolver(key_resolver);

    for (const auto& [keycode, _] : keybinds) {
      xcb_grab_key(x11.conn, false, x11.screen->root, XCB_MOD_MASK_4, keycode,
                   XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    }
  }

  using namespace std::chrono_literals;
  int tfd = MakeTimerFd(501ms);

  if (!tfd) LOG(QFATAL) << "MakeTimerFdMillis";
  absl::Cleanup tfd_closer = [tfd] { close(tfd); };

  ManageClientsStartup();

  std::vector<pollfd> fds;
  fds.emplace_back(
      pollfd{.fd = xcb_get_file_descriptor(x11.conn), .events = POLLIN});
  fds.emplace_back(pollfd{.fd = tfd, .events = POLLIN});

  NylaFs nylafs;
  if (nylafs.Init()) {
    fds.emplace_back(pollfd{.fd = nylafs.GetFd(), .events = POLLIN});

    nylafs.Register(NylaFsDynamicFile{"clients", DumpClients, [] {}});

    nylafs.Register(NylaFsDynamicFile{"quit", [] { return "quit\n"; },
                                      [&is_running] { is_running = false; }});

  } else {
    LOG(ERROR) << "could not start NylaFS, continuing anyway...";
  }

  xcb_ungrab_server(x11.conn);

  while (is_running && !xcb_connection_has_error(x11.conn)) {
    xcb_flush(x11.conn);

    ProcessWM();

    xcb_flush(x11.conn);

    if (poll(fds.data(), fds.size(), -1) == -1) {
      PLOG(ERROR) << "poll";
      break;
    }

    if (fds[0].revents & POLLIN) {
      ProcessWMEvents(is_running, modifier, keybinds);
    }

    const bool update_bar = fds[1].revents & POLLIN;
    if (update_bar || wm_bar_dirty) {
      if (update_bar) {
        uint64_t expirations;
        read(tfd, &expirations, sizeof(expirations));
      }

      UpdateBar();
    }

    if (fds.size() > 2 && fds[2].revents & POLLIN) {
      nylafs.Process();
    }
  }

  return 0;
}

}  // namespace nyla

int main(int argc, char** argv) { return nyla::Main(argc, argv); }
