#include "nyla/apps/wm/screen_saver_inhibitor.h"

#include <cstdint>
#include <string_view>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "nyla/commons/containers/map.h"
#include "nyla/dbus/dbus.h"
#include "nyla/debugfs/debugfs.h"
#include "nyla/platform/x11/platform_x11.h"
#include "xcb/screensaver.h"

namespace nyla
{

using namespace platform_x11_internal;

static uint32_t next_inhibit_cookie = 1;
static Map<uint32_t, std::string> inhibit_cookies;

static void HandleNameOwnerChange(const char *name, const char *old_owner, const char *new_owner)
{
    if (new_owner && *new_owner == '\0' && old_owner && *old_owner != '\0')
    {
        absl::erase_if(inhibit_cookies, [old_owner](auto &ent) -> auto {
            const auto &[cookie, owner] = ent;
            return owner == old_owner;
        });
    }
}

static void HandleMessage(DBusMessage *msg)
{
    if (dbus_message_is_method_call(msg, "org.freedesktop.ScreenSaver", "Inhibit"))
    {
        const char *in_name;
        const char *in_reason;

        DBusErrorWrapper err;
        if (!dbus_message_get_args(msg, err,                     //
                                   DBUS_TYPE_STRING, &in_name,   //
                                   DBUS_TYPE_STRING, &in_reason, //
                                   DBUS_TYPE_INVALID))
        {
            DBus_ReplyInvalidArguments(msg, std::move(err));
            return;
        }

        std::string_view sender = dbus_message_get_sender(msg);
        uint32_t cookie = next_inhibit_cookie++;

        auto [_, ok] = inhibit_cookies.try_emplace(cookie, sender);
        if (!ok)
            return;

        DBus_ReplyOne(msg, DBUS_TYPE_UINT32, &cookie);

        if (inhibit_cookies.size() == 1)
        {
            xcb_screensaver_suspend(x11.conn, true);
        }
        return;
    }

    if (dbus_message_is_method_call(msg, "org.freedesktop.ScreenSaver", "UnInhibit"))
    {
        uint32_t in_cookie;

        DBusErrorWrapper err;
        if (!dbus_message_get_args(msg, err,                     //
                                   DBUS_TYPE_UINT32, &in_cookie, //
                                   DBUS_TYPE_INVALID))
        {
            DBus_ReplyInvalidArguments(msg, std::move(err));
            return;
        }

        auto it = inhibit_cookies.find(in_cookie);
        if (it != inhibit_cookies.end() && it->second == dbus_message_get_sender(msg))
        {
            inhibit_cookies.erase(it);
        }

        DBus_ReplyNone(msg);

        if (inhibit_cookies.empty())
        {
            xcb_screensaver_suspend(x11.conn, false);
        }
        return;
    }

    if (dbus_message_is_method_call(msg, "org.freedesktop.ScreenSaver", "SimulateUserActivity"))
    {
        DBus_ReplyNone(msg);
        return;
    }

    LOG(INFO) << "inhibit count: " << inhibit_cookies.size();
}

static auto DumpInhibitors() -> std::string;

void ScreenSaverInhibitorInit()
{
    DBus_TrackNameOwnerChanges();

    auto handler = new DBusObjectPathHandler{
        .introspect_xml = "<node>"
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
        .message_handler = HandleMessage,
        .name_owner_changed_handler = HandleNameOwnerChange,
    };

    DBus_RegisterHandler("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", handler);
    DBus_RegisterHandler("org.freedesktop.ScreenSaver", "/ScreenSaver", handler);

    DebugFsRegister(
        "screensaver_inhibitors", nullptr,                   //
        [](auto &file) -> auto { file.content = DumpInhibitors(); }, //
        nullptr);
}

static auto DumpInhibitors() -> std::string
{
    std::string out;

    for (auto &[cookie, owner] : inhibit_cookies)
    {
        absl::StrAppendFormat(&out, "%v: %v\n", cookie, owner);
    }

    return out;
}

} // namespace nyla