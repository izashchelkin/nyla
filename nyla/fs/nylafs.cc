#include "nyla/fs/nylafs.h"

#include <sys/mount.h>
#include <sys/types.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <span>

#include "absl/log/check.h"
#include "fuse_lowlevel.h"

namespace nyla {

void NylaFs::HandleInit(fuse_conn_info *conn) {
  conn->want = FUSE_CAP_ASYNC_READ;
  conn->want &= ~FUSE_CAP_ASYNC_READ;
}

void NylaFs::HandleLookup(fuse_req_t req, fuse_ino_t parent_inode,
                          const char *name) {
  if (parent_inode != 1) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  auto it = std::find_if(
      files_.begin(), files_.end(),
      [name](const NylaFsDynamicFile &file) { return file.name == name; });
  if (it == files_.end()) {
    fuse_reply_err(req, ENOENT);
  } else {
    auto entry_param = static_cast<fuse_entry_param>(*it);
    fuse_reply_entry(req, &entry_param);
  }
}

void NylaFs::HandleGetAttr(fuse_req_t req, fuse_ino_t inode,
                           fuse_file_info *file_info) {
  auto it = std::find_if(
      files_.begin(), files_.end(),
      [inode](const NylaFsDynamicFile &file) { return file.inode == inode; });
  if (it == files_.end()) {
    fuse_reply_err(req, ENOENT);
  } else {
    auto entry_param = static_cast<fuse_entry_param>(*it);
    fuse_reply_attr(req, &entry_param.attr, 1.0);
  }
}

void NylaFs::HandleGetXAttr(fuse_req_t req, fuse_ino_t inode, const char *name,
                            size_t size) {
  fuse_reply_err(req, ENOTSUP);
}

void NylaFs::HandleSetXAttr(fuse_req_t req, fuse_ino_t inode, const char *name,
                            const char *value, size_t size, int flags) {
  fuse_reply_err(req, ENOTSUP);
}

void NylaFs::HandleRemoveXAttr(fuse_req_t req, fuse_ino_t inode,
                               const char *name) {
  fuse_reply_err(req, ENOTSUP);
}

void NylaFs::HandleOpen(fuse_req_t req, fuse_ino_t inode,
                        fuse_file_info *file_info) {
  if (inode == 1) {
    fuse_reply_err(req, EISDIR);
    return;
  }

  if ((file_info->flags & O_ACCMODE) != O_RDONLY) {
    fuse_reply_err(req, EACCES);
    return;
  }

  auto it = std::find_if(
      files_.begin(), files_.end(),
      [inode](const NylaFsDynamicFile &file) { return file.inode == inode; });
  if (it == files_.end()) {
    fuse_reply_err(req, ENOENT);
  } else {
    fuse_reply_open(req, file_info);
  }
}

void NylaFs::HandleRead(fuse_req_t req, fuse_ino_t inode, size_t size,
                        off_t offset, fuse_file_info *file_info) {
  auto it = std::find_if(
      files_.begin(), files_.end(),
      [inode](const NylaFsDynamicFile &file) { return file.inode == inode; });
  if (it == files_.end()) {
    fuse_reply_err(req, ENOENT);
  } else {
    FuseReplyBufSlice(req, it->get_content(), offset, size);
  }
}

void NylaFs::HandleReadDir(fuse_req_t req, fuse_ino_t inode, size_t size,
                           off_t offset, fuse_file_info *file_info) {
  if (inode != 1) {
    fuse_reply_err(req, ENOTDIR);
    return;
  }

  size_t total_size = 0;

  auto count = [&](const char *name) {
    total_size += fuse_add_direntry(req, nullptr, 0, name, nullptr, 0);
  };

  count(".");
  count("..");
  for (const NylaFsDynamicFile &file : files_) count(file.name);

  std::vector<char> buf(total_size);
  size_t pos = 0;

  auto append = [&](fuse_ino_t inode, const char *name) {
    struct stat stat{.st_ino = inode};
    pos += fuse_add_direntry(req, buf.data() + pos, total_size - pos, name,
                             &stat, buf.size());
  };

  append(1, ".");
  append(1, "..");
  for (const NylaFsDynamicFile &file : files_) append(file.inode, file.name);

  FuseReplyBufSlice(req, buf, offset, size);
}

bool NylaFs::Init() {
  static const char *arg = "";
  fuse_args args{.argc = 1, .argv = const_cast<char **>(&arg)};

  static fuse_lowlevel_ops op{
      .init =
          [](void *instance, fuse_conn_info *conn) {
            reinterpret_cast<NylaFs *>(instance)->HandleInit(conn);
          },
      .lookup =
          [](fuse_req_t req, fuse_ino_t parent_inode, const char *name) {
            reinterpret_cast<NylaFs *>(fuse_req_userdata(req))
                ->HandleLookup(req, parent_inode, name);
          },
      .getattr =
          [](fuse_req_t req, fuse_ino_t inode, fuse_file_info *file_info) {
            reinterpret_cast<NylaFs *>(fuse_req_userdata(req))
                ->HandleGetAttr(req, inode, file_info);
          },
      .open =
          [](fuse_req_t req, fuse_ino_t inode, fuse_file_info *file_info) {
            reinterpret_cast<NylaFs *>(fuse_req_userdata(req))
                ->HandleOpen(req, inode, file_info);
          },
      .read =
          [](fuse_req_t req, fuse_ino_t inode, size_t size, off_t offset,
             fuse_file_info *file_info) {
            reinterpret_cast<NylaFs *>(fuse_req_userdata(req))
                ->HandleRead(req, inode, size, offset, file_info);
          },
      .readdir =
          [](fuse_req_t req, fuse_ino_t inode, size_t size, off_t offset,
             fuse_file_info *file_info) {
            reinterpret_cast<NylaFs *>(fuse_req_userdata(req))
                ->HandleReadDir(req, inode, size, offset, file_info);
          },
      .setxattr =
          [](fuse_req_t req, fuse_ino_t inode, const char *name,
             const char *value, size_t size, int flags) {
            reinterpret_cast<NylaFs *>(fuse_req_userdata(req))
                ->HandleSetXAttr(req, inode, name, value, size, flags);
          },
      .getxattr =
          [](fuse_req_t req, fuse_ino_t inode, const char *name, size_t size) {
            reinterpret_cast<NylaFs *>(fuse_req_userdata(req))
                ->HandleGetXAttr(req, inode, name, size);
          },
      .removexattr =
          [](fuse_req_t req, fuse_ino_t inode, const char *name) {
            reinterpret_cast<NylaFs *>(fuse_req_userdata(req))
                ->HandleRemoveXAttr(req, inode, name);
          },
  };

  CHECK(!session);
  session = fuse_session_new(&args, &op, sizeof(op), nullptr);
  if (!session) return false;

  if (fuse_session_mount(session, "/home/izashchelkin/nylafs")) return false;

  return true;
}

int NylaFs::GetFd() { return fuse_session_fd(session); }

void NylaFs::Process() {
  fuse_buf buf{};
  int res = fuse_session_receive_buf(session, &buf);
  if (res == -EINTR) return;
  if (res <= 0) return;
  fuse_session_process_buf(session, &buf);
}

void FuseReplyEmptyBuf(fuse_req_t req) { fuse_reply_buf(req, nullptr, 0); }

void FuseReplyBufSlice(fuse_req_t req, std::span<const char> buf, off_t off,
                       size_t max_size) {
  if (off > buf.size())
    FuseReplyEmptyBuf(req);
  else
    fuse_reply_buf(req, buf.data() + off, std::min(max_size, buf.size() - off));
}

}  // namespace nyla
