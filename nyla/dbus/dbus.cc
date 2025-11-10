#include "nyla/dbus/dbus.h"

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "dbus/dbus.h"
#include "nyla/commons/containers/set.h"

namespace nyla {

DBus dbus;

void DBus_Initialize() {
  DBusErrorWrapper err;
  dbus.conn = dbus_bus_get(DBUS_BUS_SESSION, err);
  if (err.bad() || !dbus.conn) {
    LOG(ERROR) << "could not connect to dbus: " << err.inner.message;
    dbus.conn = nullptr;
    return;
  }

  dbus_connection_set_exit_on_disconnect(dbus.conn, false);
  dbus_connection_flush(dbus.conn);
}

void DBus_TrackNameOwnerChanges() {
  if (!dbus.conn) return;

  static bool called = false;
  if (!called) {
    called = true;
    dbus_bus_add_match(
        dbus.conn,
        "type='signal',sender='org.freedesktop.DBus',"
        "interface='org.freedesktop.DBus',member='NameOwnerChanged'",
        nullptr);
  }
}

void DBus_Process() {
  if (!dbus.conn) return;

  dbus_connection_read_write(dbus.conn, 0);

  for (;;) {
    DBusMessage *msg = dbus_connection_pop_message(dbus.conn);
    if (!msg) break;

    absl::Cleanup unref_msg = [=] { dbus_message_unref(msg); };

    if (dbus_message_is_signal(msg, "org.freedesktop.DBus",
                               "NameOwnerChanged")) {
      const char *name;
      const char *old_owner;
      const char *new_owner;

      DBusErrorWrapper err;
      if (!dbus_message_get_args(msg, err,                      //
                                 DBUS_TYPE_STRING, &name,       //
                                 DBUS_TYPE_STRING, &old_owner,  //
                                 DBUS_TYPE_STRING, &new_owner,  //
                                 DBUS_TYPE_INVALID)) {
        LOG(ERROR) << err.message();
        continue;
      }

      Set<DBusObjectPathHandler *> tracker;
      for (const auto &[path, handler] : dbus.handlers) {
        if (!handler->name_owner_changed_handler) continue;
        if (auto [_, ok] = tracker.emplace(handler); !ok) continue;

        handler->name_owner_changed_handler(name, old_owner, new_owner);
      }

      continue;
    }

    const char *path = dbus_message_get_path(msg);
    if (!path) {
      continue;
    }
    auto it = dbus.handlers.find(std::string_view{path});
    if (it == dbus.handlers.end()) continue;
    auto &[_, handler] = *it;

    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable",
                                    "Introspect")) {
      DBus_ReplyOne(msg, DBUS_TYPE_STRING, &handler->introspect_xml);
      continue;
    }

    handler->message_handler(msg);
  }
}

void DBus_RegisterHandler(const char *bus, const char *path,
                          DBusObjectPathHandler *handler) {
  if (!dbus.conn) return;

  CHECK(handler);

  DBusErrorWrapper err;

  int ret =
      dbus_bus_request_name(dbus.conn, bus, DBUS_NAME_FLAG_DO_NOT_QUEUE, err);
  if (err.bad() || (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER &&
                    ret != DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER)) {
    LOG(ERROR) << "could not become owner of the bus " << bus;
    return;
  }

  auto [_, ok] = dbus.handlers.try_emplace(path, handler);
  CHECK(ok);
}

void DBus_ReplyInvalidArguments(DBusMessage *msg, const DBusErrorWrapper &err) {
  DBusMessage *error_msg =
      dbus_message_new_error(msg, DBUS_ERROR_INVALID_ARGS, err.inner.message);
  dbus_connection_send(dbus.conn, error_msg, nullptr);
  dbus_message_unref(error_msg);
}

void DBus_ReplyNone(DBusMessage *msg) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  dbus_connection_send(dbus.conn, reply, nullptr);
  dbus_message_unref(reply);
}

void DBus_ReplyOne(DBusMessage *msg, int type, void *val) {
  DBusMessage *reply = dbus_message_new_method_return(msg);
  DBusMessageIter it;
  dbus_message_iter_init_append(reply, &it);
  dbus_message_iter_append_basic(&it, type, val);
  dbus_connection_send(dbus.conn, reply, nullptr);
  dbus_message_unref(reply);
}

}  // namespace nyla