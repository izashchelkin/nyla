#include "xcav/backup.h"

#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/hash.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/region_alloc.h"

#include <string.h>
namespace nyla
{

// ─── Backup ─────────────────────────────────────────────────────────────────

// Get the XCAV backup base directory: $HOME/.xcav/backups
static auto XcavBackupsDir(region_alloc &alloc) -> byteview
{
    byteview home;
    if (!TryReadEnvVar("HOME"_s, home))
        home = "/tmp"_s;
    byteview rel = "/.xcav/backups"_s;
    uint64_t totalLen = home.size + rel.size;
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, totalLen + 1);
    MemCpy(buf.data, home.data, home.size);
    MemCpy(buf.data + home.size, rel.data, rel.size);
    buf.data[totalLen] = 0;
    return byteview{buf.data, totalLen};
}
// Find the project root (git repo root or CWD) for stable hashing.
static auto ProjectRoot(region_alloc &alloc) -> byteview
{
    byteview cwd = GetCurrentDirectory(alloc);

    // Walk up from CWD to find .git
    int64_t p = (int64_t)cwd.size;
    while (p > 0)
    {
        uint64_t dirLen = (uint64_t)p;

        // Build "/.git" path
        span<uint8_t> gitPath = RegionAlloc::AllocArray<uint8_t>(alloc, dirLen + 6);
        MemCpy(gitPath.data, cwd.data, dirLen);
        gitPath.data[dirLen] = '/';
        gitPath.data[dirLen + 1] = '.';
        gitPath.data[dirLen + 2] = 'g';
        gitPath.data[dirLen + 3] = 'i';
        gitPath.data[dirLen + 4] = 't';
        gitPath.data[dirLen + 5] = 0;

        if (IsDirectory(byteview{gitPath.data, dirLen + 5}))
        {
            span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, dirLen + 1);
            MemCpy(buf.data, cwd.data, dirLen);
            buf.data[dirLen] = 0;
            return byteview{buf.data, dirLen};
        }

        // Go up one directory
        while (p > 0 && cwd.data[p - 1] != '/')
            --p;
        if (p > 0)
            --p; // skip the '/'
    }

    // No git repo found, use CWD
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, cwd.size + 1);
    MemCpy(buf.data, cwd.data, cwd.size);
    buf.data[cwd.size] = 0;
    return byteview{buf.data, cwd.size};
}
static auto EnsureBackupRoot(region_alloc &alloc) -> bool
{
    byteview home;
    if (!TryReadEnvVar("HOME"_s, home))
        home = "/tmp"_s;

    // Create ~/.xcav/
    uint64_t xcavLen = home.size + 6; // "/.xcav"
    span<uint8_t> xcavPath = RegionAlloc::AllocArray<uint8_t>(alloc, xcavLen + 1);
    MemCpy(xcavPath.data, home.data, home.size);
    MemCpy(xcavPath.data + home.size, "/.xcav", 6);
    xcavPath.data[xcavLen] = 0;
    CreateDirectory(byteview{xcavPath.data, xcavLen});

    // Create ~/.xcav/backups/
    byteview baseDir = XcavBackupsDir(alloc);
    CreateDirectory(baseDir);
    // Write .gitignore in backup base dir
    uint64_t ignoreLen = baseDir.size + 11; // "/.gitignore"
    span<uint8_t> ignorePath = RegionAlloc::AllocArray<uint8_t>(alloc, ignoreLen + 1);
    MemCpy(ignorePath.data, baseDir.data, baseDir.size);
    MemCpy(ignorePath.data + baseDir.size, "/.gitignore", 11);
    ignorePath.data[ignoreLen] = 0;

    file_handle ignoreFile = FileOpen(byteview{ignorePath.data, ignoreLen}, FileOpenMode::Write);
    if (!FileValid(ignoreFile))
    {
        LOG("ERROR: cannot write .gitignore in backup dir");
        return false;
    }

    byteview ignoreRules = "*\n!.gitignore\n"_s;
    FileWrite(ignoreFile, (uint32_t)ignoreRules.size, ignoreRules.data);
    FileClose(ignoreFile);
    return true;
}
// Build the hash-subfolder byteview for a given source path.
// Result: <XcavBackupsDir>/<project-hash>/<file-hash>
// Project hash isolates repos; file hash isolates individual files within a repo.
static auto BackupHashDir(byteview filePath, region_alloc &alloc) -> byteview
{
    byteview baseDir = XcavBackupsDir(alloc);
    byteview projectRoot = ProjectRoot(alloc);
    uint32_t projHash = HashBytes32(projectRoot);
    uint32_t fileHash = HashBytes32(filePath);
    char projStr[9], fileStr[9];
    for (int i = 7; i >= 0; --i)
    {
        projStr[i] = "0123456789abcdef"[projHash & 0xF];
        projHash >>= 4;
        fileStr[i] = "0123456789abcdef"[fileHash & 0xF];
        fileHash >>= 4;
    }
    projStr[8] = 0;
    fileStr[8] = 0;

    // <baseDir>/<projStr>/<fileStr>
    uint64_t totalLen = baseDir.size + 1 + 8 + 1 + 8;
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, totalLen + 1);
    MemCpy(buf.data, baseDir.data, baseDir.size);
    buf.data[baseDir.size] = '/';
    MemCpy(buf.data + baseDir.size + 1, projStr, 8);
    buf.data[baseDir.size + 1 + 8] = '/';
    MemCpy(buf.data + baseDir.size + 1 + 8 + 1, fileStr, 8);
    buf.data[totalLen] = 0;
    return byteview{buf.data, totalLen};
}
// Find the highest version number in the hash directory.
// Returns -1 if no backups exist.
static auto BackupLatestVersion(byteview hashDir, region_alloc &alloc) -> int32_t
{
    int32_t latest = -1;
    dir_iter *iter = DirIter::Create(alloc, hashDir);
    if (!iter)
        return -1;

    file_metadata meta;
    while (DirIter::Next(alloc, *iter, meta))
    {
        if (meta.fileName.size == 3 && meta.fileName.data[0] >= '0' && meta.fileName.data[0] <= '9' &&
            meta.fileName.data[1] >= '0' && meta.fileName.data[1] <= '9' && meta.fileName.data[2] >= '0' &&
            meta.fileName.data[2] <= '9')
        {
            int32_t v = (meta.fileName.data[0] - '0') * 100 + (meta.fileName.data[1] - '0') * 10 +
                        (meta.fileName.data[2] - '0');
            if (v > latest)
                latest = v;
        }
    }
    DirIter::Destroy(*iter);
    return latest;
}

// Build a backup path like ".xcav_backups/<hash>/NNN".
static auto BackupVersionPath(byteview hashDir, int32_t version, region_alloc &alloc) -> byteview
{
    uint64_t baseLen = hashDir.size;
    uint64_t totalLen = baseLen + 1 + 3;
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, totalLen + 1);
    MemCpy(buf.data, hashDir.data, baseLen);
    buf.data[baseLen] = '/';
    buf.data[baseLen + 1] = (uint8_t)('0' + (version / 100));
    buf.data[baseLen + 2] = (uint8_t)('0' + ((version / 10) % 10));
    buf.data[baseLen + 3] = (uint8_t)('0' + (version % 10));
    buf.data[totalLen] = 0;
    return byteview{buf.data, totalLen};
}

static const int32_t kMaxBackups = 20;

auto SaveBackup(byteview filePath, region_alloc &alloc) -> bool
{
    if (!EnsureBackupRoot(alloc))
        return false;
    // Build hash subfolder and ensure it exists.
    // Hash path is <base>/<proj-hash>/<file-hash> -- create intermediate proj-hash dir too.
    byteview hashDir = BackupHashDir(filePath, alloc);

    // Extract the parent directory (proj-hash level): hashDir minus "/<file-hash>"
    uint32_t lastSlash = 0;
    for (uint32_t i = (uint32_t)hashDir.size - 1; i > 0; --i)
    {
        if (hashDir.data[i] == '/')
        {
            lastSlash = i;
            break;
        }
    }
    if (lastSlash > 0)
    {
        span<uint8_t> parentPath = RegionAlloc::AllocArray<uint8_t>(alloc, lastSlash + 1);
        MemCpy(parentPath.data, hashDir.data, lastSlash);
        parentPath.data[lastSlash] = 0;
        CreateDirectory(byteview{parentPath.data, lastSlash});
    }

    CreateDirectory(hashDir);
    // Find next version number
    int32_t latest = BackupLatestVersion(hashDir, alloc);
    int32_t nextVersion = latest + 1;

    // Prune old backups beyond kMaxBackups
    int32_t pruneBelow = nextVersion - kMaxBackups;
    if (pruneBelow >= 0)
    {
        for (int32_t v = 0; v <= pruneBelow; ++v)
        {
            byteview oldPath = BackupVersionPath(hashDir, v, alloc);
            FileDelete(oldPath);
        }
    }

    // Build backup path
    byteview backupPath = BackupVersionPath(hashDir, nextVersion, alloc);

    // Read original file
    span<uint8_t> srcPath = RegionAlloc::AllocArray<uint8_t>(alloc, filePath.size + 1);
    MemCpy(srcPath.data, filePath.data, filePath.size);
    srcPath.data[filePath.size] = 0;

    file_handle src = FileOpen(byteview{srcPath.data, filePath.size}, FileOpenMode::Read);
    if (!FileValid(src))
    {
        LOG("ERROR: cannot read backup file '%.*s'", (int)filePath.size, filePath.data);
        return false;
    }
    byteview content = FileReadFully(alloc, src);
    FileClose(src);

    // Write backup
    file_handle dst = FileOpen(backupPath, FileOpenMode::Write);
    if (!FileValid(dst))
    {
        LOG("ERROR: cannot write backup");
        return false;
    }
    FileWrite(dst, (uint32_t)content.size, content.data);
    FileClose(dst);

    return true;
}

auto RestoreBackup(byteview filePath, region_alloc &alloc) -> bool
{
    byteview hashDir = BackupHashDir(filePath, alloc);

    // Find the latest backup version
    int32_t latest = BackupLatestVersion(hashDir, alloc);
    if (latest < 0)
    {
        LOG("ERROR: no backup found for '%.*s'", (int)filePath.size, filePath.data);
        return false;
    }

    byteview backupPath = BackupVersionPath(hashDir, latest, alloc);

    // Read backup
    file_handle src = FileOpen(backupPath, FileOpenMode::Read);
    if (!FileValid(src))
    {
        LOG("ERROR: cannot read backup");
        return false;
    }
    byteview content = FileReadFully(alloc, src);
    FileClose(src);

    // Restore to original file
    span<uint8_t> dstPath = RegionAlloc::AllocArray<uint8_t>(alloc, filePath.size + 1);
    MemCpy(dstPath.data, filePath.data, filePath.size);
    dstPath.data[filePath.size] = 0;

    file_handle dst = FileOpen(byteview{dstPath.data, filePath.size}, FileOpenMode::Write);
    if (!FileValid(dst))
    {
        LOG("ERROR: cannot write '%.*s'", (int)filePath.size, filePath.data);
        return false;
    }
    FileWrite(dst, (uint32_t)content.size, content.data);
    FileClose(dst);

    // Delete the backup version file
    FileDelete(backupPath);

    // Try to remove the hash subfolder (succeeds only if empty)
    RemoveDirectory(hashDir);
    int32_t remaining = BackupLatestVersion(hashDir, alloc) + 1;
    LOG("OK: restored from backup (%d undo levels remaining)", remaining > 0 ? remaining : 0);
    return true;
}

} // namespace nyla
