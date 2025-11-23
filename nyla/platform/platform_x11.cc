#include <cstdint>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "nyla/commons/os/clock.h"
#include "nyla/platform/abstract_input.h"
#include "nyla/platform/key_physical.h"
#include "nyla/platform/platform.h"
#include "nyla/x11/x11.h"
#include "xcb/xproto.h"

namespace nyla {

static bool should_exit = false;

static const char* XkbNameFromKey(KeyPhysical key);

void PlatformInit() {
  X11_Initialize();
}

static X11_KeyResolver key_resolver;

void PlatformMapInputBegin() {
  key_resolver = {};
  X11_InitializeKeyResolver(key_resolver);
}

void PlatformMapInput(AbstractInputMapping mapping, KeyPhysical key) {
  const char* xkb_name = XkbNameFromKey(key);
  const uint32_t keycode = X11_ResolveKeyCode(key_resolver, xkb_name);
  AbstractInputMapId(mapping, {1, keycode});
}

void PlatformMapInputEnd() {
  X11_FreeKeyResolver(key_resolver);
  key_resolver = {};
}

uint32_t PlatformCreateWindow() {
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

  return window;
}

PlatformWindowSize PlatformGetWindowSize(PlatformWindow window) {
  const xcb_get_geometry_reply_t* window_geometry =
      xcb_get_geometry_reply(x11.conn, xcb_get_geometry(x11.conn, window), nullptr);
  return {window_geometry->width, window_geometry->height};
}

void PlatformProcessEvents() {
  AbstractInputProcessFrame();

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

static const char* XkbNameFromKey(KeyPhysical key) {
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
}  // namespace nyla