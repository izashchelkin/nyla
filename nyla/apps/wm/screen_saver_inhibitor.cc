#include "nyla/apps/wm/screen_saver_inhibitor.h"

#include <cstdint>
#include <string_view>
#include <unordered_map>

#include "nyla/commons/containers/map.h"
#include "nyla/dbus/dbus.h"
#include "nyla/debugfs/debugfs.h"
#include "nyla/platform/linux/platform_linux.h"
#include "xcb/screensaver.h"

namespace nyla
{

static uint32_t nextInhibitCookie = 1;
static std::unordered_map<uint32_t, std::string> inhibitCookies;

static void HandleNameOwnerChange(const char *name, const char *oldOwner, const char *newOwner)
{
    if (newOwner && *newOwner == '\0' && oldOwner && *oldOwner != '\0')
    {
        std::erase_if(inhibitCookies, [oldOwner](auto &ent) -> auto {
            const auto &[cookie, owner] = ent;
            return owner == oldOwner;
        });
    }
}

static void HandleMessage(DBusMessage *msg)
{
    Platform::Impl *x11 = g_Platform->GetImpl();

    if (dbus_message_is_method_call(msg, "org.freedesktop.ScreenSaver", "Inhibit"))
    {
        const char *inName;
        const char *inReason;

        DBusErrorWrapper err;
        if (!dbus_message_get_args(msg, err,                    //
                                   DBUS_TYPE_STRING, &inName,   //
                                   DBUS_TYPE_STRING, &inReason, //
                                   DBUS_TYPE_INVALID))
        {
            DBusReplyInvalidArguments(msg, std::move(err));
            return;
        }

        std::string_view sender = dbus_message_get_sender(msg);
        uint32_t cookie = nextInhibitCookie++;

        auto [_, ok] = inhibitCookies.try_emplace(cookie, sender);
        if (!ok)
            return;

        DBusReplyOne(msg, DBUS_TYPE_UINT32, &cookie);

        if (inhibitCookies.size() == 1)
        {
            xcb_screensaver_suspend(x11->GetConn(), true);
        }
        return;
    }

    if (dbus_message_is_method_call(msg, "org.freedesktop.ScreenSaver", "UnInhibit"))
    {
        uint32_t inCookie;

        DBusErrorWrapper err;
        if (!dbus_message_get_args(msg, err,                    //
                                   DBUS_TYPE_UINT32, &inCookie, //
                                   DBUS_TYPE_INVALID))
        {
            DBusReplyInvalidArguments(msg, std::move(err));
            return;
        }

        auto it = inhibitCookies.find(inCookie);
        if (it != inhibitCookies.end() && it->second == dbus_message_get_sender(msg))
        {
            inhibitCookies.erase(it);
        }

        DBusReplyNone(msg);

        if (inhibitCookies.empty())
        {
            xcb_screensaver_suspend(x11->GetConn(), false);
        }
        return;
    }

    if (dbus_message_is_method_call(msg, "org.freedesktop.ScreenSaver", "SimulateUserActivity"))
    {
        DBusReplyNone(msg);
        return;
    }
}

void ScreenSaverInhibitorInit()
{
    DBusTrackNameOwnerChanges();

    auto handler = new DBusObjectPathHandler{
        .introspectXml = "<node>"
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
        .messageHandler = HandleMessage,
        .nameOwnerChangedHandler = HandleNameOwnerChange,
    };

    DBusRegisterHandler("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", handler);
    DBusRegisterHandler("org.freedesktop.ScreenSaver", "/ScreenSaver", handler);
}

} // namespace nyla