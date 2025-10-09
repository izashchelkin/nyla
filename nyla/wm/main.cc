#include <sys/poll.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "nyla/commons/rect.h"
#include "nyla/commons/spawn.h"
#include "nyla/commons/timer.h"
#include "nyla/fs/nylafs.h"
#include "nyla/protocols/atoms.h"
#include "nyla/protocols/send.h"
#include "nyla/protocols/wm_protocols.h"
#include "nyla/wm/bar_manager.h"
#include "nyla/wm/client_manager.h"
#include "nyla/wm/keyboard.h"
#include "nyla/wm/x11.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xcb/xproto.h"

namespace nyla {

static void ProcessXEvents(WMState& wm_state, const bool& is_running,
                           uint16_t modifier, std::span<Keybind> keybinds);

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

  xcb_grab_server(conn);

  xcb_screen_t* screen = xcb_aux_get_screen(conn, iscreen);

  if (xcb_request_check(
          conn,
          xcb_change_window_attributes_checked(
              conn, screen->root, XCB_CW_EVENT_MASK,
              (uint32_t[]){
                  XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                  XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                  XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                  XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                  XCB_EVENT_MASK_FOCUS_CHANGE |
                  XCB_EVENT_MASK_POINTER_MOTION}))) {
    LOG(QFATAL) << "another wm is already running";
  }

  if (!InitKeyboard(conn)) LOG(QFATAL) << "could not init keyboard";
  LOG(INFO) << "init keyboard successful";

  WMState wm_state{};
  wm_state.conn = conn;
  wm_state.screen = screen;
  wm_state.atoms = [conn] {
    static Atoms atoms = InternAtoms(conn);
    return &atoms;
  }();
  wm_state.stacks.resize(9);

  uint16_t modifier = XCB_MOD_MASK_4;
  std::vector<Keybind> keybinds;

  keybinds.emplace_back(
      "AD02", [&wm_state](xcb_timestamp_t time) { NextLayout(wm_state); });
  keybinds.emplace_back("AD03", [&wm_state](xcb_timestamp_t time) {
    MoveStackPrev(wm_state, time);
  });
  keybinds.emplace_back("AD04", [&wm_state](xcb_timestamp_t time) {
    MoveStackNext(wm_state, time);
  });
  keybinds.emplace_back(
      "AD05", [](xcb_timestamp_t time) { Spawn({{"ghostty", nullptr}}); });

  keybinds.emplace_back(
      "AC02", [](xcb_timestamp_t time) { Spawn({{"dmenu_run", nullptr}}); });
  keybinds.emplace_back("AC03", [&wm_state](xcb_timestamp_t time) {
    MoveLocalPrev(wm_state, time);
  });
  keybinds.emplace_back("AC04", [&wm_state](xcb_timestamp_t time) {
    MoveLocalNext(wm_state, time);
  });
  keybinds.emplace_back("AC05",
                        [&](xcb_timestamp_t time) { ToggleZoom(wm_state); });

  keybinds.emplace_back(
      "AB02", [&wm_state](xcb_timestamp_t time) { CloseActive(wm_state); });
  keybinds.emplace_back(
      "AB04", [&wm_state](xcb_timestamp_t) { ToggleFollow(wm_state); });

  if (!BindKeyboard(conn, screen->root, modifier, keybinds))
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

  ManageClientsStartup(wm_state);

  std::vector<pollfd> fds;
  fds.emplace_back(
      pollfd{.fd = xcb_get_file_descriptor(conn), .events = POLLIN});
  fds.emplace_back(pollfd{.fd = tfd, .events = POLLIN});

  NylaFs nylafs;
  if (nylafs.Init()) {
    fds.emplace_back(pollfd{.fd = nylafs.GetFd(), .events = POLLIN});

    nylafs.Register(NylaFsDynamicFile{
        "clients",
        [&wm_state] {
          std::string out;

          const ClientStack& stack = GetActiveStack(wm_state);

          absl::StrAppendFormat(&out, "active window = %x\n\n",
                                stack.active_window);

          for (const auto& [client_window, client] : wm_state.clients) {
            std::string_view indent = [&client]() {
              if (client.transient_for) return "  T  ";
              if (!client.subwindows.empty()) return "  S  ";
              return "";
            }();

            std::string transient_for_name;
            if (client.transient_for) {
              auto it = wm_state.clients.find(client.transient_for);
              if (it == wm_state.clients.end()) {
                transient_for_name =
                    "invalid " + std::to_string(client.transient_for);
              } else {
                transient_for_name = it->second.name + " " +
                                     std::to_string(client.transient_for);
              }
            } else {
              transient_for_name = "none";
            }

            absl::StrAppendFormat(
                &out,
                "%swindow=%x\n%sname=%v\n%srect=%v\n%swm_"
                "transient_for=%v\n%sinput=%v\n"
                "%swm_take_focus=%v\n%swm_delete_window=%v\n%"
                "ssubwindows=%v\n%smax_dimensions=%vx%v\n\n",
                indent, client_window, indent, client.name, indent, client.rect,
                indent, transient_for_name, indent, client.wm_hints_input,
                indent, client.wm_take_focus, indent, client.wm_delete_window,
                indent, absl::StrJoin(client.subwindows, ", "), indent,
                client.max_width, client.max_height);
          }
          return out;
        },
        [] {},
    });

    nylafs.Register(NylaFsDynamicFile{
        "quit",
        [] { return "quit\n"; },
        [&is_running] { is_running = false; },
    });

  } else {
    LOG(ERROR) << "could not start NylaFS, continuing anyway...";
  }

  xcb_ungrab_server(conn);

  while (is_running && !xcb_connection_has_error(conn)) {
    Rect screen_rect =
        ApplyMarginTop(Rect(screen->width_in_pixels, screen->height_in_pixels),
                       bar_manager.height());
    ApplyLayoutChanges(wm_state, screen_rect, 2);

    for (auto& [client_window, client] : wm_state.clients) {
      if (client.wants_configure_notify) {
        SendConfigureNotify(conn, client_window, wm_state.screen->root,
                            client.rect.x(), client.rect.y(),
                            client.rect.width(), client.rect.height(), 2);
        client.wants_configure_notify = false;
      }
    }

    xcb_flush(conn);

    if (poll(fds.data(), fds.size(), -1) == -1) {
      PLOG(ERROR) << "poll";
      break;
    }

    if (fds[0].revents & POLLIN) {
      ProcessXEvents(wm_state, is_running, modifier, keybinds);
    }

    if (fds[1].revents & POLLIN) {
      uint64_t expirations;
      if (read(tfd, &expirations, sizeof(expirations)) >= 0) {
        bar_manager.Update(
            conn, screen,
            GetActiveClientBarText(wm_state));  // TODO: also on Expose
      }
    }

    if (fds.size() > 2 && fds[2].revents & POLLIN) {
      nylafs.Process();
    }
  }

  return 0;
}

static void ProcessXEvents(WMState& wm_state, const bool& is_running,
                           uint16_t modifier, std::span<Keybind> keybinds) {
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
          HandleKeyPress(keypress->detail, keybinds, keypress->time);
        break;
      }
      case XCB_PROPERTY_NOTIFY: {
        // TODO:
        break;
      }
      case XCB_CONFIGURE_REQUEST: {
        auto configurerequest =
            reinterpret_cast<xcb_configure_request_event_t*>(event);
        auto it = wm_state.clients.find(configurerequest->window);
        if (it != wm_state.clients.end()) {
          it->second.wants_configure_notify = true;
        }
        break;
      }
      case XCB_MAP_REQUEST: {
        xcb_map_window(
            wm_state.conn,
            reinterpret_cast<xcb_map_request_event_t*>(event)->window);
        break;
      }
      case XCB_MAP_NOTIFY: {
        auto mapnotify = reinterpret_cast<xcb_map_notify_event_t*>(event);
        if (!mapnotify->override_redirect) {
          xcb_window_t window =
              reinterpret_cast<xcb_map_notify_event_t*>(event)->window;
          ManageClient(wm_state, window, true);
        }
        break;
      }
      case XCB_MAPPING_NOTIFY: {
        // auto mappingnotify =
        //     reinterpret_cast<xcb_mapping_notify_event_t*>(event);

        if (BindKeyboard(wm_state.conn, wm_state.screen->root, modifier,
                         keybinds))
          LOG(INFO) << "bind keyboard successful";
        else
          LOG(ERROR) << "could not bind keyboard";

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
      case XCB_FOCUS_IN: {
        auto focusin = reinterpret_cast<xcb_focus_in_event_t*>(event);
        if (focusin->mode != XCB_NOTIFY_MODE_NORMAL) {
          // LOG(INFO) << "??? focus in mode : " << int(focusin->mode);
          break;
        }

        CheckFocusTheft(wm_state);
        break;
      }
      case 0: {
        auto error = reinterpret_cast<xcb_generic_error_t*>(event);
        LOG(ERROR) << "xcb error: "
                   << static_cast<X11ErrorCode>(error->error_code)
                   << " sequence: " << error->sequence;
      }
    }
  }
}

}  // namespace nyla

int main(int argc, char** argv) { return nyla::Main(argc, argv); }
