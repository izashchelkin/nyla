#include "xcav/backup.h"

#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/hash.h"
#include "nyla/commons/region_alloc.h"

#include <sys/stat.h>
#include <unistd.h>

namespace nyla
{

// ─── Backup ─────────────────────────────────────────────────────────────────

static const char *kBackupDir = ".xcav_backups";

static auto EnsureBackupRoot() -> bool
{
    mkdir(kBackupDir, 0755);

    file_handle ignoreFile = FileOpen(".xcav_backups/.gitignore"_s, FileOpenMode::Write);
    if (!FileValid(ignoreFile))
    {
        LOG("ERROR: cannot write .xcav_backups/.gitignore");
        return false;
    }

    byteview ignoreRules = "*\n!.gitignore\n"_s;
    FileWrite(ignoreFile, (uint32_t)ignoreRules.size, ignoreRules.data);
    FileClose(ignoreFile);
    return true;
}

// Build the hash-subfolder byteview for a given source path.
// Returns a null-terminated path like ".xcav_backups/a1b2c3d4".
static auto BackupHashDir(byteview filePath, region_alloc &alloc) -> byteview
{
    uint32_t hash = HashBytes32(filePath);
    char hashStr[9];
    for (int i = 7; i >= 0; --i)
    {
        hashStr[i] = "0123456789abcdef"[hash & 0xF];
        hash >>= 4;
    }
    hashStr[8] = 0;

    uint64_t dirLen = strlen(kBackupDir);
    uint64_t totalLen = dirLen + 1 + 8;
    span<uint8_t> buf = RegionAlloc::AllocArray<uint8_t>(alloc, totalLen + 1);
    MemCpy(buf.data, kBackupDir, dirLen);
    buf.data[dirLen] = '/';
    MemCpy(buf.data + dirLen + 1, hashStr, 8);
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
    if (!EnsureBackupRoot())
        return false;

    // Build hash subfolder and ensure it exists
    byteview hashDir = BackupHashDir(filePath, alloc);
    mkdir(Span::CStr(hashDir), 0755);

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
            unlink(Span::CStr(oldPath));
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
    unlink(Span::CStr(backupPath));

    // Try to remove the hash subfolder (succeeds only if empty)
    rmdir(Span::CStr(hashDir));

    int32_t remaining = BackupLatestVersion(hashDir, alloc) + 1;
    LOG("OK: restored from backup (%d undo levels remaining)", remaining > 0 ? remaining : 0);
    return true;
}

} // namespace nyla
