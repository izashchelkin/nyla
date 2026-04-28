#include "nyla/commons/dev_shaders.h"

#include <cinttypes>
#include <cstdint>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/dev_log.h"
#include "nyla/commons/dir_watcher.h"
#include "nyla/commons/file.h"
#include "nyla/commons/file_utils.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/platform_condvar.h"
#include "nyla/commons/platform_dir_watch.h"
#include "nyla/commons/platform_mutex.h"
#include "nyla/commons/platform_thread.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace
{

struct dev_shader_root_entry
{
    byteview srcDir;
    byteview outDir;
};

constexpr inline uint64_t kQueueCap = 32;
constexpr inline uint64_t kPathCap = 512;
constexpr inline uint64_t kDirCap = 256;
constexpr inline uint64_t kNameCap = 128;
constexpr inline uint64_t kFilesCap = 256;
constexpr inline uint64_t kIncludesPerFile = 16;

struct compile_job
{
    inline_string<kPathCap> srcPath;
    inline_string<kPathCap> outPath;
    inline_string<kDirCap> dirPath;
    inline_string<kNameCap> name;
    const char *profile;
};

struct shader_file
{
    const dev_shader_root_entry *root;
    byteview relPath;
    inline_vec<uint16_t, kIncludesPerFile> includes;
};

struct dev_shaders_state
{
    region_alloc persistent;
    region_alloc workerScratch;
    region_alloc scratch;
    inline_vec<dev_shader_root_entry, 16> roots;
    inline_vec<shader_file, kFilesCap> files;

    platform_mutex *queueMutex;
    platform_condvar *queueCv;
    inline_vec<compile_job, kQueueCap> queue;

    platform_thread *worker;
};

dev_shaders_state *g_dev_shaders;

auto CopyByteview(region_alloc &alloc, byteview src) -> byteview
{
    span<uint8_t> dst = RegionAlloc::AllocArray<uint8_t>(alloc, src.size + 1);
    MemCpy(dst.data, src.data, src.size);
    dst.data[src.size] = 0;
    return byteview{dst.data, src.size};
}

template <uint64_t Cap> void WriteCStrPath(inline_string<Cap> &out, byteview dir, byteview name, byteview tail)
{
    out.size = 0;
    InlineVec::Append(out, dir);
    InlineVec::Append(out, "/"_s);
    InlineVec::Append(out, name);
    if (tail.size)
        InlineVec::Append(out, tail);
    InlineVec::Append(out, byteview{(const uint8_t *)"\0", 1});
    out.size -= 1;
}

template <uint64_t Cap> void WriteCStr(inline_string<Cap> &out, byteview src)
{
    out.size = 0;
    InlineVec::Append(out, src);
    InlineVec::Append(out, byteview{(const uint8_t *)"\0", 1});
    out.size -= 1;
}

void RunCompile(const compile_job &job)
{
    const char *argv[] = {
        "dxc",
        "-spirv",
        "-DSPIRV=1",
        "-fspv-target-env=vulkan1.3",
        "-fvk-use-dx-layout",
        "-fspv-reflect",
        "-Zi",
        "-Qembed_debug",
        "-fspv-debug=vulkan-with-source",
        "-Od",
        "-T",
        job.profile,
        "-E",
        "main",
        "-Fo",
        (const char *)job.outPath.data.data,
        (const char *)job.srcPath.data.data,
        nullptr,
    };

    RegionAlloc::Reset(g_dev_shaders->workerScratch);

    byteview log{};
    int32_t rc =
        RunSync(span<const char *const>{argv, sizeof(argv) / sizeof(argv[0])}, g_dev_shaders->workerScratch, log);

    if (rc == 0)
    {
        LOG("dev_shaders: compiled " SV_FMT "/" SV_FMT, SV_ARG((byteview)job.dirPath), SV_ARG((byteview)job.name));
    }
    else
    {
        LOG("dev_shaders: dxc failed (rc=%d) " SV_FMT "/" SV_FMT "\n" SV_FMT, rc, SV_ARG((byteview)job.dirPath),
            SV_ARG((byteview)job.name), SV_ARG(log));

        uint8_t lineBuf[256];
        uint64_t n = StringWriteFmt(span<uint8_t>{lineBuf, sizeof(lineBuf)}, "shader fail: " SV_FMT " rc=%d"_s,
                                    SV_ARG((byteview)job.name), rc);
        DevLog::Push(byteview{lineBuf, n});
    }
}

void WaitPopJob(compile_job &out)
{
    PlatformMutex::Lock(*g_dev_shaders->queueMutex);
    while (g_dev_shaders->queue.size == 0)
        PlatformCondvar::Wait(*g_dev_shaders->queueCv, *g_dev_shaders->queueMutex);
    out = g_dev_shaders->queue.data.data[0];
    if (g_dev_shaders->queue.size > 1)
        MemMove(g_dev_shaders->queue.data.data, g_dev_shaders->queue.data.data + 1,
                (g_dev_shaders->queue.size - 1) * sizeof(compile_job));
    g_dev_shaders->queue.size -= 1;
    PlatformMutex::Unlock(*g_dev_shaders->queueMutex);
}

void WorkerMain(void *)
{
    compile_job job;
    for (;;)
    {
        WaitPopJob(job);
        RunCompile(job);
    }
}

auto FindFile(const dev_shader_root_entry *root, byteview relPath) -> int32_t
{
    for (uint64_t i = 0; i < g_dev_shaders->files.size; ++i)
    {
        const shader_file &f = g_dev_shaders->files[i];
        if (f.root == root && Span::Eq(f.relPath, relPath))
            return (int32_t)i;
    }
    return -1;
}

auto FindOrInsertFile(const dev_shader_root_entry *root, byteview relPath) -> int32_t
{
    int32_t found = FindFile(root, relPath);
    if (found >= 0)
        return found;

    if (g_dev_shaders->files.size >= kFilesCap)
    {
        LOG("dev_shaders: file table full, dropping " SV_FMT, SV_ARG(relPath));
        return -1;
    }

    auto &f = InlineVec::Append(g_dev_shaders->files);
    f.root = root;
    f.relPath = CopyByteview(g_dev_shaders->persistent, relPath);
    f.includes.size = 0;
    return (int32_t)(g_dev_shaders->files.size - 1);
}

void ScanIncludes(byteview src, const dev_shader_root_entry *root, inline_vec<uint16_t, kIncludesPerFile> &out)
{
    out.size = 0;

    uint64_t i = 0;
    while (i < src.size)
    {
        while (i < src.size && (src.data[i] == ' ' || src.data[i] == '\t'))
            ++i;

        if (i + 8 <= src.size && MemEq(src.data + i, "#include", 8))
        {
            i += 8;
            while (i < src.size && (src.data[i] == ' ' || src.data[i] == '\t'))
                ++i;
            if (i < src.size && src.data[i] == '"')
            {
                ++i;
                uint64_t start = i;
                while (i < src.size && src.data[i] != '"' && src.data[i] != '\n')
                    ++i;
                if (i < src.size && src.data[i] == '"')
                {
                    byteview text{src.data + start, i - start};
                    uint64_t lastSlash = (uint64_t)-1;
                    for (uint64_t k = 0; k < text.size; ++k)
                        if (text.data[k] == '/' || text.data[k] == '\\')
                            lastSlash = k;
                    byteview base = (lastSlash == (uint64_t)-1)
                                        ? text
                                        : byteview{text.data + lastSlash + 1, text.size - lastSlash - 1};

                    int32_t idx = FindOrInsertFile(root, base);
                    if (idx >= 0)
                    {
                        bool dup = false;
                        for (uint64_t k = 0; k < out.size; ++k)
                            if (out[k] == (uint16_t)idx)
                            {
                                dup = true;
                                break;
                            }
                        if (!dup)
                        {
                            if (out.size < kIncludesPerFile)
                                InlineVec::Append(out, (uint16_t)idx);
                            else
                                LOG("dev_shaders: include cap exceeded, dropping " SV_FMT, SV_ARG(base));
                        }
                    }
                }
            }
        }

        while (i < src.size && src.data[i] != '\n')
            ++i;
        if (i < src.size)
            ++i;
    }
}

void RescanIncludes(uint16_t idx)
{
    shader_file &f = g_dev_shaders->files[idx];

    RegionAlloc::Reset(g_dev_shaders->scratch);

    auto &fullPath = RegionAlloc::AllocVec<uint8_t, 0x200>(g_dev_shaders->scratch);
    InlineVec::Append(fullPath, f.root->srcDir);
    InlineVec::Append(fullPath, "/"_s);
    InlineVec::Append(fullPath, f.relPath);
    InlineVec::Append(fullPath, byteview{(const uint8_t *)"\0", 1});
    fullPath.size -= 1;

    file_handle file = FileOpen(fullPath, FileOpenMode::Read);
    if (!FileValid(file))
    {
        f.includes.size = 0;
        return;
    }

    span<uint8_t> bytes;
    bool ok = TryFileReadFully(g_dev_shaders->scratch, file, bytes);
    FileClose(file);
    if (!ok)
    {
        f.includes.size = 0;
        return;
    }

    // ScanIncludes may insert into the files table, which can invalidate the
    // shader_file reference if the storage moves. inline_vec storage is
    // fixed-cap and never reallocates, so f stays valid; we still re-resolve
    // by index at the end to be defensive.
    ScanIncludes(byteview{bytes.data, bytes.size}, g_dev_shaders->files[idx].root, g_dev_shaders->files[idx].includes);
}

void CollectDependents(uint16_t seedIdx, inline_vec<uint16_t, kFilesCap> &out)
{
    out.size = 0;
    InlineVec::Append(out, seedIdx);

    uint64_t cursor = 0;
    while (cursor < out.size)
    {
        uint16_t cur = out[cursor++];
        for (uint64_t i = 0; i < g_dev_shaders->files.size; ++i)
        {
            bool already = false;
            for (uint64_t j = 0; j < out.size; ++j)
                if (out[j] == (uint16_t)i)
                {
                    already = true;
                    break;
                }
            if (already)
                continue;

            const shader_file &f = g_dev_shaders->files[i];
            for (uint64_t k = 0; k < f.includes.size; ++k)
            {
                if (f.includes[k] == cur)
                {
                    if (out.size < kFilesCap)
                        InlineVec::Append(out, (uint16_t)i);
                    break;
                }
            }
        }
    }
}

void EnqueueCompile(const shader_file &f)
{
    const char *profile;
    if (Span::EndsWith(f.relPath, ".vs.hlsl"_s))
        profile = "vs_6_0";
    else if (Span::EndsWith(f.relPath, ".ps.hlsl"_s))
        profile = "ps_6_0";
    else
    {
        LOG("dev_shaders: unknown stage " SV_FMT, SV_ARG(f.relPath));
        return;
    }

    compile_job pending;
    pending.profile = profile;
    WriteCStrPath(pending.srcPath, f.root->srcDir, f.relPath, byteview{});
    WriteCStrPath(pending.outPath, f.root->outDir, f.relPath, ".spv"_s);
    WriteCStr(pending.dirPath, f.root->srcDir);
    WriteCStr(pending.name, f.relPath);

    PlatformMutex::Lock(*g_dev_shaders->queueMutex);
    bool dup = false;
    for (uint64_t i = 0; i < g_dev_shaders->queue.size; ++i)
    {
        const compile_job &j = g_dev_shaders->queue.data.data[i];
        if (Span::Eq((byteview)j.srcPath, (byteview)pending.srcPath))
        {
            dup = true;
            break;
        }
    }
    bool dropped = false;
    bool pushed = false;
    if (!dup)
    {
        if (g_dev_shaders->queue.size < kQueueCap)
        {
            g_dev_shaders->queue.data.data[g_dev_shaders->queue.size++] = pending;
            pushed = true;
        }
        else
            dropped = true;
    }
    PlatformMutex::Unlock(*g_dev_shaders->queueMutex);

    if (pushed)
        PlatformCondvar::Signal(*g_dev_shaders->queueCv);
    if (dropped)
        LOG("dev_shaders: queue full, dropped " SV_FMT, SV_ARG(f.relPath));
}

void OnShaderEvent(const dir_watcher_event &ev, void *)
{
    if (!Any(ev.mask & (platform_dir_watch_event_type::Modified | platform_dir_watch_event_type::MovedTo)))
        return;

    const dev_shader_root_entry *root = nullptr;
    for (uint64_t i = 0; i < g_dev_shaders->roots.size; ++i)
    {
        if (Span::Eq(g_dev_shaders->roots[i].srcDir, ev.dirPath))
        {
            root = &g_dev_shaders->roots[i];
            break;
        }
    }
    if (!root)
        return;

    bool isHlsli = Span::EndsWith(ev.name, ".hlsli"_s);
    bool isHlsl = !isHlsli && Span::EndsWith(ev.name, ".hlsl"_s);
    if (!isHlsl && !isHlsli)
        return;

    int32_t idx = FindOrInsertFile(root, ev.name);
    if (idx < 0)
        return;

    RescanIncludes((uint16_t)idx);

    if (isHlsl)
    {
        EnqueueCompile(g_dev_shaders->files[idx]);
        return;
    }

    auto &deps = RegionAlloc::AllocVec<uint16_t, kFilesCap>(g_dev_shaders->scratch);
    deps.size = 0;
    CollectDependents((uint16_t)idx, deps);
    for (uint64_t i = 0; i < deps.size; ++i)
    {
        const shader_file &f = g_dev_shaders->files[deps[i]];
        if (Span::EndsWith(f.relPath, ".hlsl"_s))
            EnqueueCompile(f);
    }
}

auto SpvMissing(const shader_file &f) -> bool
{
    inline_string<kPathCap> path;
    WriteCStrPath(path, f.root->outDir, f.relPath, ".spv"_s);

    file_handle h = FileOpen((byteview)path, FileOpenMode::Read);
    if (FileValid(h))
    {
        FileClose(h);
        return false;
    }
    return true;
}

void ScanRoot(const dev_shader_root_entry *root)
{
    RegionAlloc::Reset(g_dev_shaders->scratch);

    dir_iter *it = DirIter::Create(g_dev_shaders->scratch, root->srcDir);
    if (!it)
        return;

    file_metadata meta;
    while (DirIter::Next(g_dev_shaders->scratch, *it, meta))
    {
        if (Any(meta.attributes & file_attribute::Hidden))
            continue;
        if (Any(meta.attributes & file_attribute::Directory))
            continue;

        bool isHlsli = Span::EndsWith(meta.fileName, ".hlsli"_s);
        bool isHlsl = !isHlsli && Span::EndsWith(meta.fileName, ".hlsl"_s);
        if (!isHlsl && !isHlsli)
            continue;

        FindOrInsertFile(root, meta.fileName);
    }

    DirIter::Destroy(*it);
}

} // namespace

namespace DevShaders
{

void API Bootstrap(span<const dev_shader_root> roots)
{
    g_dev_shaders = &RegionAlloc::Alloc<dev_shaders_state>(RegionAlloc::g_BootstrapAlloc);
    g_dev_shaders->persistent = RegionAlloc::Create(MemPagePool::kChunkSize, 0);
    g_dev_shaders->workerScratch = RegionAlloc::Create(MemPagePool::kChunkSize, 0);
    g_dev_shaders->scratch = RegionAlloc::Create(MemPagePool::kChunkSize, 0);

    g_dev_shaders->queueMutex = PlatformMutex::Create(RegionAlloc::g_BootstrapAlloc);
    g_dev_shaders->queueCv = PlatformCondvar::Create(RegionAlloc::g_BootstrapAlloc);

    DirWatcher::Subscribe(".hlsl"_s, OnShaderEvent, nullptr);
    DirWatcher::Subscribe(".hlsli"_s, OnShaderEvent, nullptr);

    for (uint64_t i = 0; i < roots.size; ++i)
    {
        byteview src = CopyByteview(g_dev_shaders->persistent, roots[i].srcDir);
        byteview out = CopyByteview(g_dev_shaders->persistent, roots[i].outDir);
        InlineVec::Append(g_dev_shaders->roots, dev_shader_root_entry{
                                                    .srcDir = src,
                                                    .outDir = out,
                                                });
        DirWatcher::WatchDir(g_dev_shaders->persistent, src);
    }

    for (uint64_t i = 0; i < g_dev_shaders->roots.size; ++i)
        ScanRoot(&g_dev_shaders->roots[i]);

    for (uint64_t i = 0; i < g_dev_shaders->files.size; ++i)
        RescanIncludes((uint16_t)i);

    uint64_t missingCount = 0;
    for (uint64_t i = 0; i < g_dev_shaders->files.size; ++i)
    {
        const shader_file &f = g_dev_shaders->files[i];
        bool isHlsli = Span::EndsWith(f.relPath, ".hlsli"_s);
        bool isHlsl = !isHlsli && Span::EndsWith(f.relPath, ".hlsl"_s);
        if (!isHlsl)
            continue;
        if (SpvMissing(f))
        {
            EnqueueCompile(f);
            ++missingCount;
        }
    }

    RegionAlloc::Reset(g_dev_shaders->scratch);

    g_dev_shaders->worker = PlatformThread::Create(RegionAlloc::g_BootstrapAlloc, &WorkerMain, nullptr);
    PlatformThread::SetName(*g_dev_shaders->worker, "nyla-shadercc");

    LOG("dev_shaders: %" PRIu64 " roots watched, %" PRIu64 " files indexed, %" PRIu64 " missing spv enqueued",
        g_dev_shaders->roots.size, g_dev_shaders->files.size, missingCount);
}

} // namespace DevShaders

} // namespace nyla
