#pragma once

#include "dbus/dbus.h"
#include "nyla/commons/containers/map.h"

namespace nyla
{

struct DBusObjectPathHandler
{
    const char *introspectXml;
    void (*messageHandler)(DBusMessage *msg);
    void (*nameOwnerChangedHandler)(const char *name, const char *oldOwner, const char *newOwner);
};

struct DBus
{
    DBusConnection *conn;
    Map<std::string_view, DBusObjectPathHandler *> handlers;
};
extern DBus dbus;

void DBusInitialize();
void DBusTrackNameOwnerChanges();
void DBusRegisterHandler(const char *bus, const char *path, DBusObjectPathHandler *handler);
void DBusProcess();

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
    auto operator=(const DBusErrorWrapper &) -> DBusErrorWrapper & = delete;

    DBusErrorWrapper(DBusErrorWrapper &&other) noexcept
    {
        dbus_error_init(&inner);
        std::swap(inner, other.inner);
    }
    auto operator=(DBusErrorWrapper &&other) noexcept -> DBusErrorWrapper &
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

    auto Bad() const -> bool
    {
        return dbus_error_is_set(&inner);
    }
    auto Name() const -> const char *
    {
        return inner.name;
    }
    auto Message() const -> const char *
    {
        return inner.message;
    }

    void Clear()
    {
        dbus_error_free(&inner);
        dbus_error_init(&inner);
    }
};

void DBusReplyInvalidArguments(DBusMessage *msg, const DBusErrorWrapper &err);
void DBusReplyNone(DBusMessage *msg);
void DBusReplyOne(DBusMessage *msg, int type, void *val);

} // namespace nyla