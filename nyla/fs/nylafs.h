#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

#include "fuse_lowlevel.h"

namespace nyla {

class NylaFsDynamicFile {
 public:
  NylaFsDynamicFile(const char *name, std::function<std::string()> get_content)
      : name_{name},
        get_content_{get_content},
        content_time_{},
        content_cache_{},
        inode_{} {}

  NylaFsDynamicFile(std::function<std::string()> get_content)
      : name_{}, get_content_{get_content}, inode_{} {}

  fuse_ino_t &inode() { return inode_; }
  fuse_ino_t inode() const { return inode_; }

  const char *name() const { return name_; }

  std::string_view get_content() {
    std::chrono::milliseconds now =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch());

    if (!content_time_.count() || (now - content_time_).count() > 100L) {
      content_time_ = now;
      content_cache_ = get_content_();
    }

    return content_cache_;
  }

  fuse_entry_param AsFuseEntryParam();

 private:
  const char *name_;
  std::function<std::string()> get_content_;
  std::chrono::milliseconds content_time_;
  std::string content_cache_;
  fuse_ino_t inode_;
  uint32_t mode_ = S_IFREG | 0444;
};

void FuseReplyEmptyBuf(fuse_req_t req);
void FuseReplyBufSlice(fuse_req_t req, std::span<const char> buf, off_t offset,
                       size_t size);

class NylaFs {
 public:
  NylaFs() : session_{} {}
  ~NylaFs();

  bool Init();
  int GetFd();
  void Process();
  void Register(const NylaFsDynamicFile &file);

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

  fuse_session *session_;
  fuse_ino_t next_inode_ = 2;
  std::vector<NylaFsDynamicFile> files_;
};

}  // namespace nyla
