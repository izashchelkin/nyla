#pragma once

#include <cstdint>
#include <string>

#define FUSE_USE_VERSION 316

#include "fuse_lowlevel.h"
#include "nyla/commons/containers/map.h"

namespace nyla {

struct DebugFsFile {
  const char* name;
  void* data;
  std::string content;
  uint64_t content_time;
  void (*set_content_handler)(DebugFsFile&);
  void (*read_notify_handler)(DebugFsFile&);
};

struct DebugFs {
  Map<ino_t, DebugFsFile> files;
  fuse_session* session;
  int fd;
};
extern DebugFs debugfs;

void DebugFsInitialize(const std::string& path);
void DebugFsProcess();
void DebugFsRegister(const char* name, void* data, void (*set_content_handler)(DebugFsFile&),
                     void (*read_notify_handler)(DebugFsFile&));
}  // namespace nyla