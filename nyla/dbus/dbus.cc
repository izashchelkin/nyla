#include "nyla/dbus/dbus.h"

#include "dbus/dbus.h"
#include "nyla/commons/containers/set.h"

namespace nyla
{

DBus dbus;

void DBusInitialize()
{
    DBusErrorWrapper err;
    dbus.conn = dbus_bus_get(DBUS_BUS_SESSION, err);
    if (err.Bad() || !dbus.conn)
    {
        NYLA_LOG("could not connect to dbus: %s", err.inner.message);
        dbus.conn = nullptr;
        return;
    }

    dbus_connection_set_exit_on_disconnect(dbus.conn, false);
    dbus_connection_flush(dbus.conn);
}

void DBusTrackNameOwnerChanges()
{
    if (!dbus.conn)
        return;

    static bool called = false;
    if (!called)
    {
        called = true;
        dbus_bus_add_match(dbus.conn,
                           "type='signal',sender='org.freedesktop.DBus',"
                           "interface='org.freedesktop.DBus',member='NameOwnerChanged'",
                           nullptr);
    }
}

void DBusProcess()
{
    if (!dbus.conn)
        return;

    dbus_connection_read_write(dbus.conn, 1);

    for (;;)
    {
        DBusMessage *msg = dbus_connection_pop_message(dbus.conn);
        if (!msg)
            break;

        Cleanup unrefMsg([=] -> void { dbus_message_unref(msg); });

        if (dbus_message_is_signal(msg, "org.freedesktop.DBus", "NameOwnerChanged"))
        {
            const char *name;
            const char *oldOwner;
            const char *newOwner;

            DBusErrorWrapper err;
            if (!dbus_message_get_args(msg, err,                    //
                                       DBUS_TYPE_STRING, &name,     //
                                       DBUS_TYPE_STRING, &oldOwner, //
                                       DBUS_TYPE_STRING, &newOwner, //
                                       DBUS_TYPE_INVALID))
            {
                NYLA_LOG("%s", err.Message());
                continue;
            }

            Set<DBusObjectPathHandler *> tracker;
            for (const auto &[path, handler] : dbus.handlers)
            {
                if (!handler->nameOwnerChangedHandler)
                    continue;
                if (auto [_, ok] = tracker.emplace(handler); !ok)
                    continue;

                handler->nameOwnerChangedHandler(name, oldOwner, newOwner);
            }

            continue;
        }

        const char *path = dbus_message_get_path(msg);
        if (!path)
        {
            continue;
        }
        auto it = dbus.handlers.find(std::string_view{path});
        if (it == dbus.handlers.end())
            continue;
        auto &[_, handler] = *it;

        if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect"))
        {
            DBusReplyOne(msg, DBUS_TYPE_STRING, &handler->introspectXml);
            continue;
        }

        handler->messageHandler(msg);
    }
}

void DBusRegisterHandler(const char *bus, const char *path, DBusObjectPathHandler *handler)
{
    if (!dbus.conn)
        return;

    NYLA_ASSERT(handler);

    DBusErrorWrapper err;

    int ret = dbus_bus_request_name(dbus.conn, bus, DBUS_NAME_FLAG_DO_NOT_QUEUE, err);
    if (err.Bad() || (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER && ret != DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER))
    {
        NYLA_LOG("could not become owner of the bus %s", bus);
        return;
    }

    auto [_, ok] = dbus.handlers.try_emplace(path, handler);
    NYLA_ASSERT(ok);
}

void DBusReplyInvalidArguments(DBusMessage *msg, const DBusErrorWrapper &err)
{
    DBusMessage *errorMsg = dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, err.inner.message);
    dbus_connection_send(dbus.conn, errorMsg, nullptr);
    dbus_message_unref(errorMsg);
}

void DBusReplyNone(DBusMessage *msg)
{
    DBusMessage *reply = dbus_message_new_method_return(msg);
    dbus_connection_send(dbus.conn, reply, nullptr);
    dbus_message_unref(reply);
}

void DBusReplyOne(DBusMessage *msg, int type, void *val)
{
    DBusMessage *reply = dbus_message_new_method_return(msg);
    DBusMessageIter it;
    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_append_basic(&it, type, val);
    dbus_connection_send(dbus.conn, reply, nullptr);
    dbus_message_unref(reply);
}

} // namespace nyla