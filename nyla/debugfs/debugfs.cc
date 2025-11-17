#include "nyla/debugfs/debugfs.h"

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <ranges>
#include <span>
#include <string_view>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "fuse_lowlevel.h"
#include "nyla/commons/os/clock.h"

namespace nyla {

DebugFs debugfs;
static ino_t next_inode = 2;

void FuseReplyEmptyBuf(fuse_req_t req) { fuse_reply_buf(req, nullptr, 0); }

void FuseReplyBufSlice(fuse_req_t req, std::span<const char> buf, off_t offset,
                       size_t size) {
  if (static_cast<size_t>(offset) > buf.size())
    FuseReplyEmptyBuf(req);
  else {
    // LOG(INFO) << "bufsize=" << buf.size() << " offset=" << offset;
    fuse_reply_buf(req, buf.data() + offset,
                   std::min(size, buf.size() - offset));
  }
}

static std::string_view GetContent(DebugFsFile &file) {
  uint64_t now = GetMonotonicTimeMicros();
  if (now - file.content_time > 10) {
    file.set_content_handler(file);
    file.content_time = GetMonotonicTimeMicros();
  }

  return file.content;
}

static fuse_entry_param MakeFileEntryParam(ino_t inode, DebugFsFile &file) {
  constexpr uint32_t mode = S_IFREG | 0444;

  return {
      .ino = inode,
      .attr =
          {
              .st_nlink = 1,
              .st_mode = mode,
              .st_size = static_cast<off_t>(GetContent(file).size()),
          },
      .attr_timeout = 1.0,
      .entry_timeout = 1.0,
  };
}

static void HandleInit(void *userdata, struct fuse_conn_info *conn) {
  // LOG(INFO) << "init";

  conn->want = FUSE_CAP_ASYNC_READ;
  conn->want &= ~FUSE_CAP_ASYNC_READ;
}

static void HandleLookup(fuse_req_t req, fuse_ino_t parent_inode,
                         const char *name) {
  // LOG(INFO) << "lookup " << parent_inode << " " << name;

  if (parent_inode != 1) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  std::string_view lookup_name = name;
  auto it = std::find_if(debugfs.files.begin(), debugfs.files.end(),
                         [lookup_name](const auto &ent) {
                           const auto &[_, file] = ent;
                           return file.name == lookup_name;
                         });
  if (it == debugfs.files.end()) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  auto &[inode, file] = *it;
  fuse_entry_param entry_param = MakeFileEntryParam(inode, file);
  fuse_reply_entry(req, &entry_param);
}

static void HandleGetAttr(fuse_req_t req, fuse_ino_t inode,
                          fuse_file_info *file_info) {
  // LOG(INFO) << "get attr " << inode;

  if (inode == 1) {
    const fuse_entry_param entry_param{
        .ino = 1,
        .attr = {.st_ino = 1,
                 .st_nlink = 2 + debugfs.files.size(),
                 .st_mode = S_IFDIR | 0444},
        .attr_timeout = 1.0,
        .entry_timeout = 1.0,
    };
    fuse_reply_attr(req, &entry_param.attr, 1.0);
  } else {
    auto it = debugfs.files.find(inode);
    if (it == debugfs.files.end()) {
      fuse_reply_err(req, ENOENT);
      return;
    }

    auto &[inode, file] = *it;
    fuse_entry_param entry_param = MakeFileEntryParam(inode, file);
    fuse_reply_attr(req, &entry_param.attr, 1.0);
  }
}

static void HandleGetXAttr(fuse_req_t req, fuse_ino_t inode, const char *name,
                           size_t size) {
  // LOG(INFO) << "get x attr";

  fuse_reply_err(req, ENOTSUP);
}

static void HandleSetXAttr(fuse_req_t req, fuse_ino_t inode, const char *name,
                           const char *value, size_t size, int flags) {
  // LOG(INFO) << "set x attr";

  fuse_reply_err(req, ENOTSUP);
}

static void HandleRemoveXAttr(fuse_req_t req, fuse_ino_t inode,
                              const char *name) {
  // LOG(INFO) << "remove x attr";

  fuse_reply_err(req, ENOTSUP);
}

static void HandleOpen(fuse_req_t req, fuse_ino_t inode,
                       fuse_file_info *file_info) {
  // LOG(INFO) << "open " << inode;

  if (inode == 1) {
    fuse_reply_err(req, EISDIR);
    return;
  }

  if ((file_info->flags & O_ACCMODE) != O_RDONLY) {
    fuse_reply_err(req, EACCES);
    return;
  }

  auto it = debugfs.files.find(inode);
  if (it == debugfs.files.end()) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  fuse_reply_open(req, file_info);
}

static void HandleRead(fuse_req_t req, fuse_ino_t inode, size_t size,
                       off_t offset, fuse_file_info *file_info) {
  // LOG(INFO) << "read " << inode << " " << size << " " << offset;

  auto it = debugfs.files.find(inode);
  if (it == debugfs.files.end()) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  auto &[_, file] = *it;
  FuseReplyBufSlice(req, GetContent(file), offset, size);

  if (file.read_notify_handler) {
    file.read_notify_handler(file);
  }
}

static void HandleReadDir(fuse_req_t req, fuse_ino_t inode, size_t size,
                          off_t offset, fuse_file_info *file_info) {
  // LOG(INFO) << "read dir " << inode << " " << size << " " << offset;

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
  for (const auto &[inode, file] : debugfs.files) {
    count(file.name);
  }

  std::vector<char> buf(total_size);
  size_t pos = 0;

  auto append = [&](fuse_ino_t inode, const char *name) {
    struct stat stat{.st_ino = inode};
    pos += fuse_add_direntry(req, buf.data() + pos, total_size - pos, name,
                             &stat, buf.size());
  };

  append(1, ".");
  append(1, "..");
  for (const auto &[inode, file] : debugfs.files) {
    append(inode, file.name);
  }

  FuseReplyBufSlice(req, buf, offset, size);
}

void DebugFsInitialize(const std::string &path) {
  const char *arg = "";
  fuse_args args{.argc = 1, .argv = const_cast<char **>(&arg)};

  const fuse_lowlevel_ops op{
      .init = HandleInit,
      .lookup = HandleLookup,
      .getattr = HandleGetAttr,
      .open = HandleOpen,
      .read = HandleRead,
      .readdir = HandleReadDir,
      .setxattr = HandleSetXAttr,
      .getxattr = HandleGetXAttr,
      .removexattr = HandleRemoveXAttr,
  };

  const char *path_cstr = path.c_str();

  {
    struct stat st = {0};
    if (stat(path_cstr, &st) == -1) {
      mkdir(path_cstr, 0700);
    }
  }

  debugfs.session = fuse_session_new(&args, &op, sizeof(op), nullptr);
  CHECK(debugfs.session);
  CHECK(!fuse_session_mount(debugfs.session, path_cstr));

  debugfs.fd = fuse_session_fd(debugfs.session);

  LOG(INFO) << "initialized debugfs";
}

void DebugFsRegister(const char *name, void *data,
                     void (*set_content_handler)(DebugFsFile &),
                     void (*read_notify_handler)(DebugFsFile &)) {
  LOG(INFO) << "registering debugfs file " << name;

  CHECK(set_content_handler);
  debugfs.files.emplace(next_inode++,
                        DebugFsFile{
                            .name = name,
                            .data = data,
                            .set_content_handler = set_content_handler,
                            .read_notify_handler = read_notify_handler,
                        });
}

void DebugFsProcess() {
  fuse_buf buf{};
  int res = fuse_session_receive_buf(debugfs.session, &buf);
  if (res == -EINTR) return;
  if (res <= 0) return;
  fuse_session_process_buf(debugfs.session, &buf);
}

}  // namespace nyla