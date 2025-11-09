#include "nyla/dbus/dbus.h"

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "dbus/dbus.h"

namespace nyla {

DBus dbus;

void DBus_Initialize() {
  dbus_error_init(&dbus.err);

  dbus.conn = dbus_bus_get(DBUS_BUS_SESSION, &dbus.err);
  if (dbus_error_is_set(&dbus.err) || !dbus.conn) {
    LOG(QFATAL) << "dbus connection error: " << dbus.err.message;
  }

  dbus_connection_set_exit_on_disconnect(dbus.conn, false);

  dbus.busfd = 0;
  dbus_connection_get_unix_fd(dbus.conn, &dbus.busfd);
  CHECK(dbus.busfd);

  dbus_connection_flush(dbus.conn);
}

void DBus_TrackNameOwnerChanges() {
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
  CHECK(!dbus_error_is_set(&dbus.err));

  dbus_connection_read_write(dbus.conn, 0);

  for (;;) {
    CHECK(!dbus_error_is_set(&dbus.err));

    DBusMessage *msg = dbus_connection_pop_message(dbus.conn);
    if (!msg) break;

    absl::Cleanup unref_msg = [=] { dbus_message_unref(msg); };

    LOG(INFO) << "got dbus message " << dbus_message_get_path(msg) << " "
              << dbus_message_get_interface(msg) << " "
              << dbus_message_get_member(msg);

    if (dbus_message_is_signal(msg, "org.freedesktop.DBus",
                               "NameOwnerChanged")) {
      const char *name;
      const char *old_owner;
      const char *new_owner;

      DBusError err;
      dbus_error_init(&err);

      dbus_message_get_args(msg, &err,                     //
                            DBUS_TYPE_STRING, &name,       //
                            DBUS_TYPE_STRING, &old_owner,  //
                            DBUS_TYPE_STRING, &new_owner,  //
                            DBUS_TYPE_INVALID);

      if (!dbus_error_is_set(&err)) {
        LOG(INFO) << "name owner changed from " << old_owner << " to "
                  << new_owner << " on " << name;

        for (const auto &[path, handler] : dbus.handlers) {
          if (handler->name_owner_changed_handler)
            handler->name_owner_changed_handler(name, old_owner, new_owner);
        }
      }

      dbus_error_free(&err);

      return;
    }

    std::string_view path = dbus_message_get_path(msg);
    auto it = dbus.handlers.find(path);
    if (it == dbus.handlers.end()) return;
    auto &[_, handler] = *it;

    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable",
                                    "Introspect")) {
      DBusMessage *reply = dbus_message_new_method_return(msg);
      DBusMessageIter it;
      dbus_message_iter_init_append(reply, &it);

      dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING,
                                     &handler->interospect_xml);

      dbus_connection_send(dbus.conn, reply, nullptr);
      dbus_message_unref(reply);
      return;
    }

    handler->message_handler(msg);
  }
}

void DBus_RegisterHandler(const char *bus, const char *path,
                          DBusObjectPathHandler *handler) {
  CHECK(handler);

  if (!handler->owns_bus) {
    CHECK_EQ(dbus_bus_request_name(dbus.conn, bus, DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                   &dbus.err),
             DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER);
    handler->owns_bus = true;
  }

  CHECK(!dbus_error_is_set(&dbus.err));

  auto [_, ok] = dbus.handlers.try_emplace(path, handler);
  CHECK(ok);
}

}  // namespace nyla