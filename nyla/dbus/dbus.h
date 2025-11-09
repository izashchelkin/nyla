#pragma once

#include "dbus/dbus.h"
#include "nyla/commons/containers/map.h"

namespace nyla {

struct DBusObjectPathHandler {
  const char* interospect_xml;
  void (*message_handler)(DBusMessage* msg);
  void (*name_owner_changed_handler)(const char* name, const char* old_owner,
                                     const char* new_owner);
  bool owns_bus;
};

struct DBus {
  DBusError err;
  DBusConnection* conn;
  int busfd;

  Map<std::string_view, DBusObjectPathHandler*> handlers;
};
extern DBus dbus;

void DBus_Initialize();
void DBus_TrackNameOwnerChanges();
void DBus_RegisterHandler(const char* bus, const char* path,
                          DBusObjectPathHandler* handler);
void DBus_Process();

}  // namespace nyla