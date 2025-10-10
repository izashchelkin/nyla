#include <sys/poll.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <limits>
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

static void ProcessXEvents(const bool& is_running, uint16_t modifier,
                           std::span<Keybind> keybinds);

int Main(int argc, char** argv) {
  bool is_running = true;

  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  int iscreen;
  wm_conn = xcb_connect(nullptr, &iscreen);
  if (xcb_connection_has_error(wm_conn)) {
    LOG(QFATAL) << "could not connect to X server";
  }

  xcb_grab_server(wm_conn);

  wm_screen = xcb_aux_get_screen(wm_conn, iscreen);

  if (xcb_request_check(
          wm_conn,
          xcb_change_window_attributes_checked(
              wm_conn, wm_screen->root, XCB_CW_EVENT_MASK,
              (uint32_t[]){
                  XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
                  XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                  XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                  XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                  XCB_EVENT_MASK_FOCUS_CHANGE |
                  XCB_EVENT_MASK_POINTER_MOTION}))) {
    LOG(QFATAL) << "another wm is already running";
  }

  if (!InitKeyboard(wm_conn)) LOG(QFATAL) << "could not init keyboard";
  LOG(INFO) << "init keyboard successful";

  atoms = InternAtoms(wm_conn);
	InitializeWM();

  uint16_t modifier = XCB_MOD_MASK_4;
  std::vector<Keybind> keybinds;

  keybinds.emplace_back("AD02", [](xcb_timestamp_t time) { NextLayout(); });
  keybinds.emplace_back("AD03",
                        [](xcb_timestamp_t time) { MoveStackPrev(time); });
  keybinds.emplace_back("AD04",
                        [](xcb_timestamp_t time) { MoveStackNext(time); });
  keybinds.emplace_back(
      "AD05", [](xcb_timestamp_t time) { Spawn({{"ghostty", nullptr}}); });

  keybinds.emplace_back(
      "AC02", [](xcb_timestamp_t time) { Spawn({{"dmenu_run", nullptr}}); });
  keybinds.emplace_back("AC03",
                        [](xcb_timestamp_t time) { MoveLocalPrev(time); });
  keybinds.emplace_back("AC04",
                        [](xcb_timestamp_t time) { MoveLocalNext(time); });
  keybinds.emplace_back("AC05", [](xcb_timestamp_t time) { ToggleZoom(); });

  keybinds.emplace_back("AB02", [](xcb_timestamp_t time) { CloseActive(); });
  keybinds.emplace_back("AB04", [](xcb_timestamp_t) { ToggleFollow(); });

  if (!BindKeyboard(wm_conn, wm_screen->root, modifier, keybinds))
    LOG(QFATAL) << "could not bind keyboard";
  LOG(INFO) << "bind keyboard successful";

  BarManager bar_manager;
  if (!bar_manager.Init(wm_conn, wm_screen)) {
    LOG(QFATAL) << "could not init bar";
  }

  using namespace std::chrono_literals;
  int tfd = MakeTimerFd(501ms);

  if (!tfd) LOG(QFATAL) << "MakeTimerFdMillis";
  absl::Cleanup tfd_closer = [tfd] { close(tfd); };

  ManageClientsStartup();

  std::vector<pollfd> fds;
  fds.emplace_back(
      pollfd{.fd = xcb_get_file_descriptor(wm_conn), .events = POLLIN});
  fds.emplace_back(pollfd{.fd = tfd, .events = POLLIN});

  NylaFs nylafs;
  if (nylafs.Init()) {
    fds.emplace_back(pollfd{.fd = nylafs.GetFd(), .events = POLLIN});

    nylafs.Register(NylaFsDynamicFile{
        "clients",
        [] {
          std::string out;

          const ClientStack& stack = GetActiveStack();

          absl::StrAppendFormat(&out, "active window = %x\n\n",
                                stack.active_window);

          for (const auto& [client_window, client] : wm_clients) {
            std::string_view indent = [&client]() {
              if (client.transient_for) return "  T  ";
              if (!client.subwindows.empty()) return "  S  ";
              return "";
            }();

            std::string transient_for_name;
            if (client.transient_for) {
              auto it = wm_clients.find(client.transient_for);
              if (it == wm_clients.end()) {
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

  xcb_ungrab_server(wm_conn);

  while (is_running && !xcb_connection_has_error(wm_conn)) {
    xcb_flush(wm_conn);

    for (auto& [client_window, client] : wm_clients) {
      for (auto& [property, cookie] : client.property_cookies) {
        xcb_get_property_reply_t* reply =
            xcb_get_property_reply(wm_conn, cookie, nullptr);

        auto handler_it = wm_property_change_handlers.find(property);
        if (handler_it == wm_property_change_handlers.end()) {
          LOG(ERROR) << "missing property change handler " << property;
          continue;
        }

        handler_it->second(client_window, client, reply);
      }
      client.property_cookies.clear();
    }

    ProcessPendingClients();

    Rect screen_rect = ApplyMarginTop(
        Rect(wm_screen->width_in_pixels, wm_screen->height_in_pixels),
        bar_manager.height());
    ApplyLayoutChanges(screen_rect, 2);

    for (auto& [client_window, client] : wm_clients) {
      if (client.wants_configure_notify) {
        SendConfigureNotify(wm_conn, client_window, wm_screen->root,
                            client.rect.x(), client.rect.y(),
                            client.rect.width(), client.rect.height(), 2);
        client.wants_configure_notify = false;
      }
    }

    xcb_flush(wm_conn);

    if (poll(fds.data(), fds.size(), -1) == -1) {
      PLOG(ERROR) << "poll";
      break;
    }

    if (fds[0].revents & POLLIN) {
      ProcessXEvents(is_running, modifier, keybinds);
    }

    if (fds[1].revents & POLLIN) {
      uint64_t expirations;
      if (read(tfd, &expirations, sizeof(expirations)) >= 0) {
        bar_manager.Update(wm_conn, wm_screen,
                           GetActiveClientBarText());  // TODO: also on Expose
      }
    }

    if (fds.size() > 2 && fds[2].revents & POLLIN) {
      nylafs.Process();
    }
  }

  return 0;
}

static void ProcessXEvents(const bool& is_running, uint16_t modifier,
                           std::span<Keybind> keybinds) {
  while (is_running) {
    xcb_generic_event_t* event = xcb_poll_for_event(wm_conn);
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
        auto propertynotify =
            reinterpret_cast<xcb_property_notify_event_t*>(event);

        xcb_window_t client_window = propertynotify->window;
        auto client_it = wm_clients.find(client_window);
        if (client_it == wm_clients.end()) break;

        xcb_atom_t property = propertynotify->atom;
        auto handler_it = wm_property_change_handlers.find(property);
        if (handler_it == wm_property_change_handlers.end()) break;

        xcb_get_property_cookie_t cookie = xcb_get_property(
            wm_conn, false, client_window, property, XCB_ATOM_ANY, 0,
            std::numeric_limits<uint32_t>::max());
        client_it->second.property_cookies[property] = cookie;
        break;
      }
      case XCB_CONFIGURE_REQUEST: {
        auto configurerequest =
            reinterpret_cast<xcb_configure_request_event_t*>(event);
        auto it = wm_clients.find(configurerequest->window);
        if (it != wm_clients.end()) {
          it->second.wants_configure_notify = true;
        }
        break;
      }
      case XCB_MAP_REQUEST: {
        xcb_map_window(
            wm_conn, reinterpret_cast<xcb_map_request_event_t*>(event)->window);
        break;
      }
      case XCB_MAP_NOTIFY: {
        auto mapnotify = reinterpret_cast<xcb_map_notify_event_t*>(event);
        if (!mapnotify->override_redirect) {
          xcb_window_t window =
              reinterpret_cast<xcb_map_notify_event_t*>(event)->window;
          ManageClient(window);
        }
        break;
      }
      case XCB_MAPPING_NOTIFY: {
        if (BindKeyboard(wm_conn, wm_screen->root, modifier, keybinds))
          LOG(INFO) << "bind keyboard successful";
        else
          LOG(ERROR) << "could not bind keyboard";

        break;
      }
      case XCB_UNMAP_NOTIFY: {
        UnmanageClient(
            reinterpret_cast<xcb_unmap_notify_event_t*>(event)->window);
        break;
      }
      case XCB_DESTROY_NOTIFY: {
        UnmanageClient(
            reinterpret_cast<xcb_destroy_notify_event_t*>(event)->window);
        break;
      }
      case XCB_FOCUS_IN: {
        if (reinterpret_cast<xcb_focus_in_event_t*>(event)->mode ==
            XCB_NOTIFY_MODE_NORMAL) {
          CheckFocusTheft();
        }

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
