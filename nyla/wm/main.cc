#include <sys/poll.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <variant>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/os/spawn.h"
#include "nyla/commons/os/timerfd.h"
#include "nyla/dbus/dbus.h"
#include "nyla/debugfs/debugfs.h"
#include "nyla/wm/window_manager.h"
#include "nyla/x11/x11.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

int Main(int argc, char** argv) {
  InitLogging();

  bool is_running = true;

  X11_Initialize();

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

  DBus_Initialize();
  DebugFsInitialize(argv[0] + std::string("-debugfs"));
  InitializeWM();

  uint16_t modifier = XCB_MOD_MASK_4;
  std::vector<
      std::pair<xcb_keycode_t,
                std::variant<void (*)(xcb_timestamp_t timestamp), void (*)()>>>
      keybinds;

  {
    X11_KeyResolver key_resolver;
    X11_InitializeKeyResolver(key_resolver);

    // W
    keybinds.emplace_back(X11_ResolveKeyCode(key_resolver, "AD02"), NextLayout);
    // E
    keybinds.emplace_back(X11_ResolveKeyCode(key_resolver, "AD03"),
                          MoveStackPrev);
    // R
    keybinds.emplace_back(X11_ResolveKeyCode(key_resolver, "AD04"),
                          MoveStackNext);
    // D
    keybinds.emplace_back(X11_ResolveKeyCode(key_resolver, "AC03"),
                          MoveLocalPrev);
    // F
    keybinds.emplace_back(X11_ResolveKeyCode(key_resolver, "AC04"),
                          MoveLocalNext);
    // G
    keybinds.emplace_back(X11_ResolveKeyCode(key_resolver, "AC05"), ToggleZoom);
    // X
    keybinds.emplace_back(X11_ResolveKeyCode(key_resolver, "AB02"),
                          CloseActive);
    // V
    keybinds.emplace_back(X11_ResolveKeyCode(key_resolver, "AB04"),
                          ToggleFollow);
    // T
    keybinds.emplace_back(
        X11_ResolveKeyCode(key_resolver, "AD05"),
        [](xcb_timestamp_t time) { Spawn({{"ghostty", nullptr}}); });
    // S
    keybinds.emplace_back(
        X11_ResolveKeyCode(key_resolver, "AC02"),
        [](xcb_timestamp_t time) { Spawn({{"dmenu_run", nullptr}}); });

    X11_FreeKeyResolver(key_resolver);

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
  fds.emplace_back(pollfd{
      .fd = xcb_get_file_descriptor(x11.conn),
      .events = POLLIN,
  });
  fds.emplace_back(pollfd{
      .fd = tfd,
      .events = POLLIN,
  });
  fds.emplace_back(pollfd{
      .fd = debugfs.fd,
      .events = POLLIN,
  });

  DebugFsRegister(
      "quit", &is_running,  //
      [](auto& file) { file.content = "quit\n"; },
      [](auto& file) {
        *reinterpret_cast<bool*>(file.data) = false;
        LOG(INFO) << "exit requested";
      });

  xcb_ungrab_server(x11.conn);

  while (is_running && !xcb_connection_has_error(x11.conn)) {
    DBus_Process();

    xcb_flush(x11.conn);
    ProcessWM();
    xcb_flush(x11.conn);

    if (poll(fds.data(), fds.size(), -1) == -1) {
      continue;
    }

    if (fds[0].revents & POLLIN) {
      ProcessWMEvents(is_running, modifier, keybinds);
    }

    const bool update_bar = fds[1].revents & POLLIN;
    if (update_bar) {
      uint64_t expirations;
      read(tfd, &expirations, sizeof(expirations));
    }
    if (update_bar || wm_bar_dirty) {
      UpdateBar();
    }

    if (fds[2].revents & POLLIN) {
      DebugFsProcess();
    }
  }

  return 0;
}

}  // namespace nyla

int main(int argc, char** argv) { return nyla::Main(argc, argv); }