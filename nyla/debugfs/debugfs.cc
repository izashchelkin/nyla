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

#include "fuse_lowlevel.h"
#include "nyla/commons/os/clock.h"

namespace nyla
{

DebugFs debugfs;
static ino_t nextInode = 2;

void FuseReplyEmptyBuf(fuse_req_t req)
{
    fuse_reply_buf(req, nullptr, 0);
}

void FuseReplyBufSlice(fuse_req_t req, std::span<const char> buf, off_t offset, size_t size)
{
    if (static_cast<size_t>(offset) > buf.size())
        FuseReplyEmptyBuf(req);
    else
    {
        fuse_reply_buf(req, buf.data() + offset, std::min(size, buf.size() - offset));
    }
}

static auto GetContent(DebugFsFile &file) -> std::string_view
{
    uint64_t now = GetMonotonicTimeMicros();
    if (now - file.contentTime > 10)
    {
        file.setContentHandler(file);
        file.contentTime = GetMonotonicTimeMicros();
    }

    return file.content;
}

static auto MakeFileEntryParam(ino_t inode, DebugFsFile &file) -> fuse_entry_param
{
    constexpr uint32_t kMode = S_IFREG | 0444;

    return {
        .ino = inode,
        .attr =
            {
                .st_nlink = 1,
                .st_mode = kMode,
                .st_size = static_cast<off_t>(GetContent(file).size()),
            },
        .attr_timeout = 1.0,
        .entry_timeout = 1.0,
    };
}

static void HandleInit(void *userdata, struct fuse_conn_info *conn)
{

    conn->want = FUSE_CAP_ASYNC_READ;
    conn->want &= ~FUSE_CAP_ASYNC_READ;
}

static void HandleLookup(fuse_req_t req, fuse_ino_t parentInode, const char *name)
{
    if (parentInode != 1)
    {
        fuse_reply_err(req, ENOENT);
        return;
    }

    std::string_view lookupName = name;
    auto it = std::find_if(debugfs.files.begin(), debugfs.files.end(), [lookupName](const auto &ent) -> auto {
        const auto &[_, file] = ent;
        return file.name == lookupName;
    });
    if (it == debugfs.files.end())
    {
        fuse_reply_err(req, ENOENT);
        return;
    }

    auto &[inode, file] = *it;
    fuse_entry_param entryParam = MakeFileEntryParam(inode, file);
    fuse_reply_entry(req, &entryParam);
}

static void HandleGetAttr(fuse_req_t req, fuse_ino_t inode, fuse_file_info *fileInfo)
{
    if (inode == 1)
    {
        const fuse_entry_param entryParam{
            .ino = 1,
            .attr = {.st_ino = 1, .st_nlink = 2 + debugfs.files.size(), .st_mode = S_IFDIR | 0444},
            .attr_timeout = 1.0,
            .entry_timeout = 1.0,
        };
        fuse_reply_attr(req, &entryParam.attr, 1.0);
    }
    else
    {
        auto it = debugfs.files.find(inode);
        if (it == debugfs.files.end())
        {
            fuse_reply_err(req, ENOENT);
            return;
        }

        auto &[inode, file] = *it;
        fuse_entry_param entryParam = MakeFileEntryParam(inode, file);
        fuse_reply_attr(req, &entryParam.attr, 1.0);
    }
}

static void HandleGetXAttr(fuse_req_t req, fuse_ino_t inode, const char *name, size_t size)
{
    fuse_reply_err(req, ENOTSUP);
}

static void HandleSetXAttr(fuse_req_t req, fuse_ino_t inode, const char *name, const char *value, size_t size,
                           int flags)
{
    fuse_reply_err(req, ENOTSUP);
}

static void HandleRemoveXAttr(fuse_req_t req, fuse_ino_t inode, const char *name)
{
    fuse_reply_err(req, ENOTSUP);
}

static void HandleOpen(fuse_req_t req, fuse_ino_t inode, fuse_file_info *fileInfo)
{
    if (inode == 1)
    {
        fuse_reply_err(req, EISDIR);
        return;
    }

    if ((fileInfo->flags & O_ACCMODE) != O_RDONLY)
    {
        fuse_reply_err(req, EACCES);
        return;
    }

    auto it = debugfs.files.find(inode);
    if (it == debugfs.files.end())
    {
        fuse_reply_err(req, ENOENT);
        return;
    }

    fuse_reply_open(req, fileInfo);
}

static void HandleRead(fuse_req_t req, fuse_ino_t inode, size_t size, off_t offset, fuse_file_info *fileInfo)
{
    auto it = debugfs.files.find(inode);
    if (it == debugfs.files.end())
    {
        fuse_reply_err(req, ENOENT);
        return;
    }

    auto &[_, file] = *it;
    FuseReplyBufSlice(req, GetContent(file), offset, size);

    if (file.readNotifyHandler)
    {
        file.readNotifyHandler(file);
    }
}

static void HandleReadDir(fuse_req_t req, fuse_ino_t inode, size_t size, off_t offset, fuse_file_info *fileInfo)
{
    if (inode != 1)
    {
        fuse_reply_err(req, ENOTDIR);
        return;
    }

    size_t totalSize = 0;

    auto count = [&](const char *name) -> void { totalSize += fuse_add_direntry(req, nullptr, 0, name, nullptr, 0); };

    count(".");
    count("..");
    for (const auto &[inode, file] : debugfs.files)
    {
        count(file.name);
    }

    std::vector<char> buf(totalSize);
    size_t pos = 0;

    auto append = [&](fuse_ino_t inode, const char *name) -> void {
        struct stat stat{.st_ino = inode};
        pos += fuse_add_direntry(req, buf.data() + pos, totalSize - pos, name, &stat, buf.size());
    };

    append(1, ".");
    append(1, "..");
    for (const auto &[inode, file] : debugfs.files)
    {
        append(inode, file.name);
    }

    FuseReplyBufSlice(req, buf, offset, size);
}

void DebugFsInitialize(const std::string &path)
{
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

    const char *pathCstr = path.c_str();

    {
        struct stat st = {0};
        if (stat(pathCstr, &st) == -1)
        {
            mkdir(pathCstr, 0700);
        }
    }

    debugfs.session = fuse_session_new(&args, &op, sizeof(op), nullptr);
    NYLA_ASSERT(debugfs.session);
    NYLA_ASSERT(!fuse_session_mount(debugfs.session, pathCstr));

    debugfs.fd = fuse_session_fd(debugfs.session);

    NYLA_LOG("initialized debugfs");
}

void DebugFsRegister(const char *name, void *data, void (*setContentHandler)(DebugFsFile &),
                     void (*readNotifyHandler)(DebugFsFile &))
{
    NYLA_LOG("registering debugfs file %s", name);

    CHECK(setContentHandler);
    debugfs.files.emplace(nextInode++, DebugFsFile{
                                           .name = name,
                                           .data = data,
                                           .setContentHandler = setContentHandler,
                                           .readNotifyHandler = readNotifyHandler,
                                       });
}

void DebugFsProcess()
{
    fuse_buf buf{};
    int res = fuse_session_receive_buf(debugfs.session, &buf);
    if (res == -EINTR)
        return;
    if (res <= 0)
        return;
    fuse_session_process_buf(debugfs.session, &buf);
}

} // namespace nyla