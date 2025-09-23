#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>

#include "fuse_lowlevel.h"

namespace nyla {

struct NylaFsDynamicFile {
  const char *name;
  std::function<std::string()> get_content;
  fuse_ino_t inode;
  uint32_t mode = S_IFREG | 0444;

  explicit operator fuse_entry_param() {
    return {
        .ino = inode,
        .attr = {.st_nlink = 1, .st_mode = mode, .st_size = 1},
        .attr_timeout = 1.0,
        .entry_timeout = 1.0,
    };
  }
};

void FuseReplyEmptyBuf(fuse_req_t req);
void FuseReplyBufSlice(fuse_req_t req, std::span<const char> buf, off_t offset,
                       size_t size);

class NylaFs {
 public:
  bool Init();
  int GetFd();
  void Process();

 private:
  void HandleInit(fuse_conn_info *conn);

  void HandleLookup(fuse_req_t req, fuse_ino_t parent_inode, const char *name);
  void HandleGetAttr(fuse_req_t req, fuse_ino_t inode,
                     fuse_file_info *file_info);
  void HandleGetXAttr(fuse_req_t req, fuse_ino_t inode, const char *name,
                      size_t size);
  void HandleSetXAttr(fuse_req_t req, fuse_ino_t inode, const char *name,
                      const char *value, size_t size, int flags);
  void HandleRemoveXAttr(fuse_req_t req, fuse_ino_t inode, const char *name);

  void HandleOpen(fuse_req_t req, fuse_ino_t inode, fuse_file_info *file_info);
  void HandleRead(fuse_req_t req, fuse_ino_t inode, size_t size, off_t offset,
                  fuse_file_info *file_info);
  void HandleReadDir(fuse_req_t req, fuse_ino_t inode, size_t size,
                     off_t offset, fuse_file_info *file_info);

  fuse_session *session;
  std::vector<NylaFsDynamicFile> files_;
};

}  // namespace nyla
