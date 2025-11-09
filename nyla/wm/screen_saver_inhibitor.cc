#include "nyla/wm/screen_saver_inhibitor.h"

#include <cstdint>
#include <string_view>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/commons/containers/map.h"
#include "nyla/dbus/dbus.h"
#include "nyla/x11/x11.h"
#include "xcb/screensaver.h"

namespace nyla {

namespace {

static uint32_t next_inhibit_cookie = 1;
static Map<uint32_t, std::string_view> inhibit_cookies;

static void Handler(DBusMessage* msg) {
  LOG(INFO) << "here";

  if (dbus_message_is_method_call(msg, "org.freedesktop.ScreenSaver",
                                  "Inhibit")) {
    DBusError err;
    dbus_error_init(&err);
    absl::Cleanup unref_err = [&err] { dbus_error_free(&err); };

    const char* in_name;
    const char* in_reason;
    dbus_message_get_args(msg, &err,                     //
                          DBUS_TYPE_STRING, &in_name,    //
                          DBUS_TYPE_STRING, &in_reason,  //
                          DBUS_TYPE_INVALID);

    std::string_view name = in_name;
    std::string_view sender = dbus_message_get_sender(msg);

    uint32_t cookie = next_inhibit_cookie++;

    LOG(INFO) << "ScreenSaverInhibit " << in_name << "(" << sender << ") "
              << in_reason;

    auto [_, ok] = inhibit_cookies.try_emplace(cookie, sender);
    CHECK(ok);

    if (!inhibit_cookies.empty()) {
      xcb_screensaver_suspend(x11.conn, true);
    }
    return;
  }

  if (dbus_message_is_method_call(msg, "org.freedesktop.ScreenSaver",
                                  "UnInhibit")) {
    DBusError err;
    dbus_error_init(&err);
    absl::Cleanup unref_err = [&err] { dbus_error_free(&err); };

    uint32_t in_cookie;
    dbus_message_get_args(msg, &err,                    //
                          DBUS_TYPE_INT32, &in_cookie,  //
                          DBUS_TYPE_INVALID);

    auto it = inhibit_cookies.find(in_cookie);
    if (it != inhibit_cookies.end() &&
        it->second == dbus_message_get_sender(msg)) {
      inhibit_cookies.erase(it);
    }

    if (inhibit_cookies.empty()) {
      xcb_screensaver_suspend(x11.conn, false);
    }
    return;
  }

  LOG(INFO) << "inhibit count: " << inhibit_cookies.size();
}

}  // namespace

void ScreenSaverInhibitorInit() {
  DBus_TrackNameOwnerChanges();

  auto handler = new DBusObjectPathHandler{
      .interospect_xml =
          "<node>"
          " <interface name='org.freedesktop.ScreenSaver'>"
          "  <method name='Inhibit'>"
          "   <arg name='application' type='s' direction='in'/>"
          "   <arg name='reason' type='s' direction='in'/>"
          "   <arg name='cookie' type='u' direction='out'/>"
          "  </method>"
          "  <method name='UnInhibit'>"
          "   <arg name='cookie' type='u' direction='in'/>"
          "  </method>"
          "  <method name='SimulateUserActivity'/>"
          " </interface>"
          " <interface name='org.freedesktop.DBus.Introspectable'>"
          "  <method name='Introspect'>"
          "   <arg name='xml' type='s' direction='out'/>"
          "  </method>"
          " </interface>"
          "</node>",
      .message_handler = Handler,
  };

  DBus_RegisterHandler("org.freedesktop.ScreenSaver",
                       "/org/freedesktop/ScreenSaver", handler);
  DBus_RegisterHandler("org.freedesktop.ScreenSaver", "/ScreenSaver", handler);
}

}  // namespace nyla