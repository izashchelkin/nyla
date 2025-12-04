#pragma once

#include "dbus/dbus.h"
#include "nyla/commons/containers/map.h"

namespace nyla
{

struct DBusObjectPathHandler
{
    const char *introspect_xml;
    void (*message_handler)(DBusMessage *msg);
    void (*name_owner_changed_handler)(const char *name, const char *old_owner, const char *new_owner);
};

struct DBus
{
    DBusConnection *conn;
    Map<std::string_view, DBusObjectPathHandler *> handlers;
};
extern DBus dbus;

void DBus_Initialize();
void DBus_TrackNameOwnerChanges();
void DBus_RegisterHandler(const char *bus, const char *path, DBusObjectPathHandler *handler);
void DBus_Process();

struct DBusErrorWrapper
{
    DBusError inner;

    DBusErrorWrapper()
    {
        dbus_error_init(&inner);
    }
    ~DBusErrorWrapper()
    {
        dbus_error_free(&inner);
    }

    DBusErrorWrapper(const DBusErrorWrapper &) = delete;
    DBusErrorWrapper &operator=(const DBusErrorWrapper &) = delete;

    DBusErrorWrapper(DBusErrorWrapper &&other) noexcept
    {
        dbus_error_init(&inner);
        std::swap(inner, other.inner);
    }
    DBusErrorWrapper &operator=(DBusErrorWrapper &&other) noexcept
    {
        if (this != &other)
        {
            dbus_error_free(&inner);
            dbus_error_init(&inner);
            std::swap(inner, other.inner);
        }
        return *this;
    }

    operator DBusError *()
    {
        return &inner;
    }
    operator const DBusError *() const
    {
        return &inner;
    }

    bool bad() const
    {
        return dbus_error_is_set(&inner);
    }
    const char *name() const
    {
        return inner.name;
    }
    const char *message() const
    {
        return inner.message;
    }

    void clear()
    {
        dbus_error_free(&inner);
        dbus_error_init(&inner);
    }
};

void DBus_ReplyInvalidArguments(DBusMessage *msg, const DBusErrorWrapper &err);
void DBus_ReplyNone(DBusMessage *msg);
void DBus_ReplyOne(DBusMessage *msg, int type, void *val);

} // namespace nyla