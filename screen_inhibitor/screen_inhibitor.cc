#include <cstring>
#include <sys/poll.h>

#include <dbus/dbus.h>
#include <xcb/screensaver.h>

#include "nyla/commons/array.h"
#include "nyla/commons/entrypoint.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/platform_linux.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

namespace
{

// ─── Data ────────────────────────────────────────────────────────────────────

static constexpr const char *kIntrospectXml = "<node>"
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
                                              "</node>";

struct inhibit_entry
{
    uint32_t cookie;
    inline_string<64> sender;
};
static_assert(sizeof(inhibit_entry) == 80);

struct screen_inhibitor
{
    DBusConnection *dbusConn;
    uint32_t nextCookie;
    inline_vec<inhibit_entry, 16> *entries;
};

screen_inhibitor *inhibitor;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static auto SenderEq(const inline_string<64> &stored, const char *cstr) -> bool
{
    uint64_t len = strlen(cstr);
    if (stored.size != len)
        return false;
    return MemEq(stored.data.data, (const uint8_t *)cstr, len);
}

static void ReplyNone(DBusMessage *msg)
{
    DBusMessage *reply = dbus_message_new_method_return(msg);
    dbus_connection_send(inhibitor->dbusConn, reply, nullptr);
    dbus_message_unref(reply);
}

static void ReplyUint32(DBusMessage *msg, uint32_t val)
{
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter it;
    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &val);
    dbus_connection_send(inhibitor->dbusConn, reply, nullptr);
    dbus_message_unref(reply);
}

static void ReplyString(DBusMessage *msg, const char *str)
{
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter it;
    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &str);
    dbus_connection_send(inhibitor->dbusConn, reply, nullptr);
    dbus_message_unref(reply);
}

static void ReplyError(DBusMessage *msg, const char *name, const char *message)
{
    DBusMessage *reply = dbus_message_new_error(msg, name, message);
    dbus_connection_send(inhibitor->dbusConn, reply, nullptr);
    dbus_message_unref(reply);
}

// ─── Message dispatch ────────────────────────────────────────────────────────

static void HandleMessage(DBusMessage *msg)
{
    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect"))
    {
        ReplyString(msg, kIntrospectXml);
        return;
    }

    if (dbus_message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged"))
    {
        const char *name = nullptr;
        const char *oldOwner = nullptr;
        const char *newOwner = nullptr;

        DBusError err;
        dbus_error_init(&err);
        dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &oldOwner, DBUS_TYPE_STRING,
                              &newOwner, DBUS_TYPE_INVALID);
        bool argsOk = !dbus_error_is_set(&err);
        dbus_error_free(&err);

        if (argsOk && newOwner && *newOwner == '\0' && oldOwner && *oldOwner != '\0')
        {
            auto &entries = *inhibitor->entries;
            uint64_t prevSize = entries.size;

            for (uint64_t i = 0; i < entries.size;)
            {
                if (SenderEq(entries[i].sender, oldOwner))
                {
                    entries[i] = entries[entries.size - 1];
                    --entries.size;
                }
                else
                {
                    ++i;
                }
            }

            if (prevSize > 0 && entries.size == 0)
            {
                xcb_screensaver_suspend(X11GetConn(), false);
                X11Flush();
            }
        }
        return;
    }

    const char *path = dbus_message_get_path(msg);
    if (!path)
        return;
    if (strcmp(path, "/org/freedesktop/ScreenSaver") != 0 && strcmp(path, "/ScreenSaver") != 0)
        return;

    if (dbus_message_is_method_call(msg, "org.freedesktop.ScreenSaver", "Inhibit"))
    {
        const char *appName = nullptr;
        const char *reason = nullptr;

        DBusError err;
        dbus_error_init(&err);
        if (!dbus_message_get_args(msg, &err, DBUS_TYPE_STRING, &appName, DBUS_TYPE_STRING, &reason, DBUS_TYPE_INVALID))
        {
            ReplyError(msg, DBUS_ERROR_INVALID_ARGS, err.message);
            dbus_error_free(&err);
            return;
        }
        dbus_error_free(&err);

        auto &entries = *inhibitor->entries;
        ASSERT(entries.size < 16);

        const char *sender = dbus_message_get_sender(msg);
        uint64_t senderLen = strlen(sender);
        ASSERT(senderLen < 64);

        inhibit_entry &entry = InlineVec::Append(entries);
        MemZero(&entry, sizeof(inhibit_entry));
        entry.cookie = inhibitor->nextCookie++;
        MemCpy(entry.sender.data.data, (const uint8_t *)sender, senderLen);
        entry.sender.size = senderLen;

        if (entries.size == 1)
        {
            xcb_screensaver_suspend(X11GetConn(), true);
            X11Flush();
        }

        ReplyUint32(msg, entry.cookie);
        return;
    }

    if (dbus_message_is_method_call(msg, "org.freedesktop.ScreenSaver", "UnInhibit"))
    {
        uint32_t cookie = 0;

        DBusError err;
        dbus_error_init(&err);
        if (!dbus_message_get_args(msg, &err, DBUS_TYPE_UINT32, &cookie, DBUS_TYPE_INVALID))
        {
            ReplyError(msg, DBUS_ERROR_INVALID_ARGS, err.message);
            dbus_error_free(&err);
            return;
        }
        dbus_error_free(&err);

        auto &entries = *inhibitor->entries;
        const char *sender = dbus_message_get_sender(msg);

        for (uint64_t i = 0; i < entries.size; ++i)
        {
            if (entries[i].cookie == cookie && SenderEq(entries[i].sender, sender))
            {
                entries[i] = entries[entries.size - 1];
                --entries.size;
                break;
            }
        }

        ReplyNone(msg);

        if (entries.size == 0)
        {
            xcb_screensaver_suspend(X11GetConn(), false);
            X11Flush();
        }
        return;
    }

    if (dbus_message_is_method_call(msg, "org.freedesktop.ScreenSaver", "SimulateUserActivity"))
    {
        ReplyNone(msg);
        return;
    }
}

} // namespace

// ─── Entry point ─────────────────────────────────────────────────────────────

void UserMain()
{
    inhibitor = &RegionAlloc::Alloc<screen_inhibitor>(RegionAlloc::g_BootstrapAlloc);
    inhibitor->entries = &RegionAlloc::AllocVec<inhibit_entry, 16>(RegionAlloc::g_BootstrapAlloc);
    inhibitor->nextCookie = 1;

    DBusError err;
    dbus_error_init(&err);
    inhibitor->dbusConn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    ASSERT(!dbus_error_is_set(&err) && inhibitor->dbusConn);
    dbus_error_free(&err);

    dbus_connection_set_exit_on_disconnect(inhibitor->dbusConn, false);

    dbus_bus_add_match(inhibitor->dbusConn,
                       "type='signal',sender='org.freedesktop.DBus',"
                       "interface='org.freedesktop.DBus',member='NameOwnerChanged'",
                       nullptr);

    {
        dbus_error_init(&err);
        int ret = dbus_bus_request_name(inhibitor->dbusConn, "org.freedesktop.ScreenSaver", DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                        &err);
        ASSERT(!dbus_error_is_set(&err) &&
               (ret == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER || ret == DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER));
        dbus_error_free(&err);
    }

    dbus_connection_flush(inhibitor->dbusConn);

    for (;;)
    {
        if (!dbus_connection_read_write(inhibitor->dbusConn, 0))
            break;

        DBusMessage *msg;
        while ((msg = dbus_connection_pop_message(inhibitor->dbusConn)) != nullptr)
        {
            HandleMessage(msg);
            dbus_message_unref(msg);
        }

        if (dbus_connection_get_dispatch_status(inhibitor->dbusConn) == DBUS_DISPATCH_DATA_REMAINS)
            continue;

        int fd;
        if (!dbus_connection_get_unix_fd(inhibitor->dbusConn, &fd))
            break;

        pollfd pfd{.fd = fd, .events = POLLIN};
        poll(&pfd, 1, -1);
    }
}

} // namespace nyla
