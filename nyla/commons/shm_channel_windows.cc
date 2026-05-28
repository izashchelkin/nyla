#include "nyla/commons/shm_channel.h"

#include "nyla/commons/fmt.h"
#include "nyla/commons/headers_windows.h"
#include "nyla/commons/intrin.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/region_alloc.h"

namespace nyla
{

// Layout in shared memory: [seq:u32][_pad:u32][data:u8[dataSize]]
// seq odd = writer active, even+increment = consistent snapshot.
static constexpr uint64_t kHeaderSize = 8;

struct shm_channel
{
    HANDLE mapping;
    uint32_t *seq; // points into mapped region
    uint8_t *data; // points into mapped region, after header
    uint64_t dataSize;
    void *mapped;
    uint64_t mappedSize;
    bool isWriter;
};

namespace ShmChannel
{

static auto FullName(const char *name, wchar_t *out, int outChars) -> void
{
    // Use Local\ namespace so the mapping is session-local.
    // Convert ASCII name to wide string prefix: "Local\name"
    out[0] = L'L';
    out[1] = L'o';
    out[2] = L'c';
    out[3] = L'a';
    out[4] = L'l';
    out[5] = L'\\';

    int i = 0;
    while (name[i] && i < outChars - 7)
    {
        out[6 + i] = (wchar_t)(uint8_t)name[i];
        ++i;
    }
    out[6 + i] = 0;
}

auto API CreateWriter(const char *name, uint64_t dataSize, region_alloc &alloc) -> shm_channel *
{
    uint64_t totalSize = kHeaderSize + dataSize;

    wchar_t wname[256];
    FullName(name, wname, 256);

    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, (DWORD)(totalSize >> 32),
                                        (DWORD)totalSize, wname);
    ASSERT(mapping != NULL, "CreateFileMappingW failed: %lu", GetLastError());

    void *mapped = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, (SIZE_T)totalSize);
    ASSERT(mapped != NULL, "MapViewOfFile failed: %lu", GetLastError());

    MemZero(mapped, totalSize);

    auto &self = RegionAlloc::Alloc<shm_channel>(alloc);
    self.mapping = mapping;
    self.seq = static_cast<uint32_t *>(mapped);
    self.data = static_cast<uint8_t *>(mapped) + kHeaderSize;
    self.dataSize = dataSize;
    self.mapped = mapped;
    self.mappedSize = totalSize;
    self.isWriter = true;

    return &self;
}

auto API OpenReader(const char *name, uint64_t dataSize, region_alloc &alloc) -> shm_channel *
{
    uint64_t totalSize = kHeaderSize + dataSize;

    wchar_t wname[256];
    FullName(name, wname, 256);

    HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, wname);
    ASSERT(mapping != NULL, "OpenFileMappingW failed: %lu (is the writer running?)", GetLastError());

    void *mapped = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, (SIZE_T)totalSize);
    ASSERT(mapped != NULL, "MapViewOfFile failed: %lu", GetLastError());

    auto &self = RegionAlloc::Alloc<shm_channel>(alloc);
    self.mapping = mapping;
    self.seq = static_cast<uint32_t *>(mapped);
    self.data = static_cast<uint8_t *>(mapped) + kHeaderSize;
    self.dataSize = dataSize;
    self.mapped = mapped;
    self.mappedSize = totalSize;
    self.isWriter = false;

    return &self;
}

void API Close(shm_channel &self)
{
    if (self.mapped)
        UnmapViewOfFile(self.mapped);
    if (self.mapping)
        CloseHandle(self.mapping);

    MemZero(&self);
}

void *API BeginWrite(shm_channel &self)
{
    ASSERT(self.isWriter);
    // Increment to odd (write in progress).
    AtomicStore32(self.seq, AtomicLoad32(self.seq) + 1);
    // Prevent subsequent non-atomic stores (the data payload) from being
    // reordered before the seq store. Redundant on x86 (TSO) but correct
    // for weakly-ordered ISAs.
    _ReadWriteBarrier();
    return self.data;
}

void API EndWrite(shm_channel &self)
{
    ASSERT(self.isWriter);
    // Prevent prior non-atomic stores (the data payload) from being
    // reordered after the seq store. Redundant on x86 but correct
    // for weakly-ordered ISAs.
    _ReadWriteBarrier();
    // Increment again to even (write complete).
    AtomicStore32(self.seq, AtomicLoad32(self.seq) + 1);
}

auto API TryRead(shm_channel &self, void *dst) -> bool
{
    ASSERT(!self.isWriter);

    // Read seq before copying
    uint32_t before = AtomicLoad32(self.seq);
    if (before & 1)
        return false; // write in progress

    // Compiler barrier to prevent reordering of the copy before the seq load
    _ReadWriteBarrier();

    MemCpy(dst, self.data, self.dataSize);

    // Compiler barrier to prevent reordering of the seq load before the copy
    _ReadWriteBarrier();

    uint32_t after = AtomicLoad32(self.seq);
    return before == after;
}

} // namespace ShmChannel

} // namespace nyla
