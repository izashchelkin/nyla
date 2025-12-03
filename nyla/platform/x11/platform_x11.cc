#include "nyla/platform/x11/platform_x11.h"

#include <sys/inotify.h>
#include <unistd.h>

#include <cstdint>
#include <string_view>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "nyla/commons/memory/optional.h"
#include "nyla/commons/os/clock.h"
#include "nyla/platform/abstract_input.h"
#include "nyla/platform/key_physical.h"
#include "nyla/platform/platform.h"
#include "xcb/xcb.h"
#include "xcb/xcb_aux.h"
#include "xcb/xinput.h"
#include "xcb/xproto.h"
#include "xkbcommon/xkbcommon-x11.h"
#include "xkbcommon/xkbcommon.h"

// Source - https://stackoverflow.com/a/72177598
// Posted by HolyBlackCat, modified by community. See post 'Timeline' for change history
// Retrieved 2025-11-17, License - CC BY-SA 4.0

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define explicit explicit_
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <xcb/xkb.h>

#undef explicit

namespace nyla {

using namespace platform_x11_internal;

namespace {

bool should_exit = false;

}

void PlatformInit() {
  X11_Initialize();
}

static X11_KeyResolver key_resolver;

void PlatformMapInputBegin() {
  key_resolver = {};
  X11_InitializeKeyResolver(key_resolver);
}

void PlatformMapInput(AbstractInputMapping mapping, KeyPhysical key) {
  const char* xkb_name = ConvertKeyPhysicalIntoXkbName(key);
  const uint32_t keycode = X11_ResolveKeyCode(key_resolver, xkb_name);
  AbstractInputMapId(mapping, {1, keycode});
}

void PlatformMapInputEnd() {
  X11_FreeKeyResolver(key_resolver);
  key_resolver = {};
}

PlatformWindow PlatformCreateWindow() {
  xcb_window_t window = xcb_generate_id(x11.conn);

  CHECK(!xcb_request_check(
      x11.conn, xcb_create_window_checked(
                    x11.conn, XCB_COPY_FROM_PARENT, window, x11.screen->root, 0, 0, x11.screen->width_in_pixels,
                    x11.screen->height_in_pixels, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, x11.screen->root_visual,
                    XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
                    (uint32_t[]){false, XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                                            XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE})));

  xcb_map_window(x11.conn, window);
  xcb_flush(x11.conn);

  return {window};
}

PlatformWindowSize PlatformGetWindowSize(PlatformWindow window) {
  const xcb_get_geometry_reply_t* window_geometry =
      xcb_get_geometry_reply(x11.conn, xcb_get_geometry(x11.conn, window.handle), nullptr);
  return {window_geometry->width, window_geometry->height};
}

static int fs_notify_fd = 0;
static std::array<PlatformFsChange, 16> fs_changes;
static uint32_t fs_changes_at = 0;
static std::vector<std::string> fs_watched;

void PlatformFsWatch(const std::string& filepath) {
  if (!fs_notify_fd) {
    fs_notify_fd = inotify_init1(IN_NONBLOCK);
    CHECK_GT(fs_notify_fd, 0);
  }

  fs_watched.emplace_back(filepath);
}

std::span<PlatformFsChange> PlatformFsGetChanges() {
  return fs_changes;
}

void PlatformProcessEvents() {
  AbstractInputProcessFrame();

  if (fs_notify_fd) {
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    char* bufp = buf;

    int numread;
    while ((numread = read(fs_notify_fd, buf, sizeof(buf))) > 0) {
      while (bufp != buf + numread) {
        inotify_event* event = reinterpret_cast<inotify_event*>(bufp);
        bufp += sizeof(inotify_event);

        std::string path = {bufp, strlen(bufp)};
        bufp += event->len;

        fs_changes[fs_changes_at] = {
            .isdir = static_cast<bool>(event->mask & IN_ISDIR),
            .path = path,
        };
        fs_changes_at = (fs_changes_at + 1) % fs_changes.size();
      }
    }
  }

  for (;;) {
    if (xcb_connection_has_error(x11.conn)) {
      should_exit = true;
      break;
    }

    xcb_generic_event_t* event = xcb_poll_for_event(x11.conn);
    if (!event) break;

    absl::Cleanup event_freer = [=]() { free(event); };
    const uint8_t event_type = event->response_type & 0x7F;

    uint64_t now = GetMonotonicTimeMicros();

    switch (event_type) {
      case XCB_KEY_PRESS: {
        auto keypress = reinterpret_cast<xcb_key_press_event_t*>(event);
        AbstractInputHandlePressed({1, keypress->detail}, now);
        break;
      }

      case XCB_KEY_RELEASE: {
        auto keyrelease = reinterpret_cast<xcb_key_release_event_t*>(event);
        AbstractInputHandleReleased({1, keyrelease->detail});
        break;
      }

      case XCB_BUTTON_PRESS: {
        auto buttonpress = reinterpret_cast<xcb_button_press_event_t*>(event);
        AbstractInputHandlePressed({2, buttonpress->detail}, now);
        break;
      }

      case XCB_BUTTON_RELEASE: {
        auto buttonrelease = reinterpret_cast<xcb_button_release_event_t*>(event);
        AbstractInputHandleReleased({2, buttonrelease->detail});
        break;
      }

      case XCB_CLIENT_MESSAGE: {
        auto clientmessage = reinterpret_cast<xcb_client_message_event_t*>(event);

        if (clientmessage->format == 32 && clientmessage->type == x11.atoms.wm_protocols &&
            clientmessage->data.data32[0] == x11.atoms.wm_delete_window) {
          should_exit = true;
        }
        break;
      }
    }
  }
}

bool PlatformShouldExit() {
  return should_exit;
}

namespace platform_x11_internal {

const char* ConvertKeyPhysicalIntoXkbName(KeyPhysical key) {
  using K = KeyPhysical;

  switch (key) {
    case K::Escape:
      return "ESC";
    case K::Grave:
      return "TLDE";

    case K::Digit1:
      return "AE01";
    case K::Digit2:
      return "AE02";
    case K::Digit3:
      return "AE03";
    case K::Digit4:
      return "AE04";
    case K::Digit5:
      return "AE05";
    case K::Digit6:
      return "AE06";
    case K::Digit7:
      return "AE07";
    case K::Digit8:
      return "AE08";
    case K::Digit9:
      return "AE09";
    case K::Digit0:
      return "AE10";
    case K::Minus:
      return "AE11";
    case K::Equal:
      return "AE12";
    case K::Backspace:
      return "BKSP";

    case K::Tab:
      return "TAB";
    case K::Q:
      return "AD01";
    case K::W:
      return "AD02";
    case K::E:
      return "AD03";
    case K::R:
      return "AD04";
    case K::T:
      return "AD05";
    case K::Y:
      return "AD06";
    case K::U:
      return "AD07";
    case K::I:
      return "AD08";
    case K::O:
      return "AD09";
    case K::P:
      return "AD10";
    case K::LeftBracket:
      return "AD11";
    case K::RightBracket:
      return "AD12";
    case K::Enter:
      return "RTRN";

    case K::CapsLock:
      return "CAPS";
    case K::A:
      return "AC01";
    case K::S:
      return "AC02";
    case K::D:
      return "AC03";
    case K::F:
      return "AC04";
    case K::G:
      return "AC05";
    case K::H:
      return "AC06";
    case K::J:
      return "AC07";
    case K::K:
      return "AC08";
    case K::L:
      return "AC09";
    case K::Semicolon:
      return "AC10";
    case K::Apostrophe:
      return "AC11";

    case K::LeftShift:
      return "LFSH";
    case K::Z:
      return "AB01";
    case K::X:
      return "AB02";
    case K::C:
      return "AB03";
    case K::V:
      return "AB04";
    case K::B:
      return "AB05";
    case K::N:
      return "AB06";
    case K::M:
      return "AB07";
    case K::Comma:
      return "AB08";
    case K::Period:
      return "AB09";
    case K::Slash:
      return "AB10";
    case K::RightShift:
      return "RTSH";

    case K::LeftCtrl:
      return "LCTL";
    case K::LeftAlt:
      return "LALT";
    case K::Space:
      return "SPCE";
    case K::RightAlt:
      return "RALT";
    case K::RightCtrl:
      return "RCTL";

    case K::ArrowLeft:
      return "LEFT";
    case K::ArrowRight:
      return "RGHT";
    case K::ArrowUp:
      return "UP";
    case K::ArrowDown:
      return "DOWN";

    case K::F1:
      return "FK01";
    case K::F2:
      return "FK02";
    case K::F3:
      return "FK03";
    case K::F4:
      return "FK04";
    case K::F5:
      return "FK05";
    case K::F6:
      return "FK06";
    case K::F7:
      return "FK07";
    case K::F8:
      return "FK08";
    case K::F9:
      return "FK09";
    case K::F10:
      return "FK10";
    case K::F11:
      return "FK11";
    case K::F12:
      return "FK12";

    case K::Unknown:
    default:
      return nullptr;
  }
}

X11_State x11;

void X11_Initialize() {
  int iscreen;
  x11.conn = xcb_connect(nullptr, &iscreen);
  if (xcb_connection_has_error(x11.conn)) {
    LOG(QFATAL) << "could not connect to X server";
  }

  x11.screen = xcb_aux_get_screen(x11.conn, iscreen);
  CHECK(x11.screen);

#define X(atom) x11.atoms.atom = X11_InternAtom(x11.conn, absl::AsciiStrToUpper(#atom));
  Nyla_X11_Atoms(X)
#undef X

  {
    uint16_t major_xkb_version_out, minor_xkb_version_out;
    uint8_t base_event_out, base_error_out;

    if (!xkb_x11_setup_xkb_extension(x11.conn, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION,
                                     XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, &major_xkb_version_out,
                                     &minor_xkb_version_out, &base_event_out, &base_error_out)) {
      LOG(QFATAL) << "could not set up xkb extension";
    }

    if (major_xkb_version_out < XKB_X11_MIN_MAJOR_XKB_VERSION ||
        (major_xkb_version_out == XKB_X11_MIN_MAJOR_XKB_VERSION &&
         minor_xkb_version_out < XKB_X11_MIN_MINOR_XKB_VERSION)) {
      LOG(QFATAL) << "could not set up xkb extension";
    }

    xcb_generic_error_t* err = nullptr;
    if (!Unown(xcb_xkb_per_client_flags_reply(
            x11.conn,
            xcb_xkb_per_client_flags(x11.conn, XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
                                     XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 0, 0, 0),
            &err)) ||
        err) {
      LOG(QFATAL) << "could not set up detectable autorepeat";
    }
  }

  {
    x11.ext_xi2 = xcb_get_extension_data(x11.conn, &xcb_input_id);
    if (!x11.ext_xi2 || !x11.ext_xi2->present) {
      LOG(QFATAL) << "could nolt set up XI2 extension";
    }

    struct {
      xcb_input_event_mask_t event_mask;
      uint32_t mask_bits;
    } mask;

    mask.event_mask.deviceid = XCB_INPUT_DEVICE_ALL_MASTER;
    mask.event_mask.mask_len = 1;
    mask.mask_bits = XCB_INPUT_XI_EVENT_MASK_RAW_MOTION;

    if (xcb_request_check(x11.conn,
                          xcb_input_xi_select_events_checked(x11.conn, x11.screen->root, 1, &mask.event_mask))) {
      LOG(QFATAL) << "could not setup XI2 extension";
    }
  }
}

xcb_window_t X11_CreateWindow(uint32_t width, uint32_t height, bool override_redirect, xcb_event_mask_t event_mask) {
  const xcb_window_t window = xcb_generate_id(x11.conn);

  xcb_create_window(x11.conn, XCB_COPY_FROM_PARENT, window, x11.screen->root, 0, 0, width, height, 0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, x11.screen->root_visual,
                    XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, (uint32_t[]){override_redirect, event_mask});

  xcb_map_window(x11.conn, window);

  return window;
}

void X11_Flush() {
  xcb_flush(x11.conn);
}

xcb_atom_t X11_InternAtom(xcb_connection_t* conn, std::string_view name,

                          bool only_if_exists) {
  xcb_intern_atom_reply_t* reply =
      xcb_intern_atom_reply(conn, xcb_intern_atom(conn, only_if_exists, name.size(), name.data()), nullptr);
  absl::Cleanup reply_freer = [reply] {
    if (reply) free(reply);
  };

  if (!reply || !reply->atom) {
    LOG(FATAL) << "could not intern atom " << name;
  }

  return reply->atom;
}

void X11_SendClientMessage32(xcb_window_t window, xcb_atom_t type, xcb_atom_t arg1, uint32_t arg2, uint32_t arg3,
                             uint32_t arg4) {
  xcb_client_message_event_t event = {
      .response_type = XCB_CLIENT_MESSAGE,
      .format = 32,
      .window = window,
      .type = type,
      .data = {.data32 = {arg1, arg2, arg3, arg4}},
  };

  xcb_send_event(x11.conn, false, window, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char*>(&event));
}

void X11_Send_WM_Take_Focus(xcb_window_t window, uint32_t time) {
  X11_SendClientMessage32(window, x11.atoms.wm_protocols, x11.atoms.wm_take_focus, time, 0, 0);
}

void X11_Send_WM_Delete_Window(xcb_window_t window) {
  X11_SendClientMessage32(window, x11.atoms.wm_protocols, x11.atoms.wm_delete_window, XCB_CURRENT_TIME, 0, 0);
}

void X11_SendConfigureNotify(xcb_window_t window, xcb_window_t parent, int16_t x, int16_t y, uint16_t width,
                             uint16_t height, uint16_t border_width) {
  xcb_configure_notify_event_t event = {
      .response_type = XCB_CONFIGURE_NOTIFY,
      .window = window,
      .x = x,
      .y = y,
      .width = width,
      .height = height,
      .border_width = border_width,
  };

  xcb_send_event(x11.conn, false, window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, reinterpret_cast<const char*>(&event));
}

//

bool X11_InitializeKeyResolver(X11_KeyResolver& key_resolver) {
  key_resolver.ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!key_resolver.ctx) return false;

  const int32_t device_id = xkb_x11_get_core_keyboard_device_id(x11.conn);
  if (device_id == -1) return false;

  key_resolver.keymap =
      xkb_x11_keymap_new_from_device(key_resolver.ctx, x11.conn, device_id, XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!key_resolver.keymap) return false;

  return true;
}

void X11_FreeKeyResolver(X11_KeyResolver& key_resolver) {
  xkb_keymap_unref(key_resolver.keymap);
  xkb_context_unref(key_resolver.ctx);
}

xcb_keycode_t X11_ResolveKeyCode(const X11_KeyResolver& key_resolver, std::string_view keyname) {
  const xkb_keycode_t keycode = xkb_keymap_key_by_name(key_resolver.keymap, keyname.data());
  CHECK(xkb_keycode_is_legal_x11(keycode));
  return keycode;
}

}  // namespace platform_x11_internal

}  // namespace nyla