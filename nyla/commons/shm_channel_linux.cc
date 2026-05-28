#include "nyla/commons/shm_channel.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nyla/commons/fmt.h"
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
    int fd;
    uint32_t *seq; // points into mapped region
    uint8_t *data; // points into mapped region, after header
    uint64_t dataSize;
    void *mapped;
    uint64_t mappedSize;
    bool isWriter;
};

namespace ShmChannel
{

auto API CreateWriter(const char *name, uint64_t dataSize, region_alloc &alloc) -> shm_channel *
{
    uint64_t totalSize = kHeaderSize + dataSize;

    int fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    ASSERT(fd >= 0, "shm_open failed: %s", strerror(errno));

    if (ftruncate(fd, (off_t)totalSize) != 0)
    {
        close(fd);
        shm_unlink(name);
        ASSERT(false, "ftruncate failed: %s", strerror(errno));
    }

    void *mapped = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT(mapped != MAP_FAILED, "mmap failed: %s", strerror(errno));

    MemZero(mapped, totalSize);

    auto &self = RegionAlloc::Alloc<shm_channel>(alloc);
    self.fd = fd;
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

    int fd = shm_open(name, O_RDONLY, 0);
    ASSERT(fd >= 0, "shm_open failed: %s (is the writer running?)", strerror(errno));

    void *mapped = mmap(nullptr, totalSize, PROT_READ, MAP_SHARED, fd, 0);
    ASSERT(mapped != MAP_FAILED, "mmap failed: %s", strerror(errno));

    auto &self = RegionAlloc::Alloc<shm_channel>(alloc);
    self.fd = fd;
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
        munmap(self.mapped, self.mappedSize);
    if (self.fd >= 0)
        close(self.fd);

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
    asm volatile("" ::: "memory");
    return self.data;
}

void API EndWrite(shm_channel &self)
{
    ASSERT(self.isWriter);
    // Prevent prior non-atomic stores (the data payload) from being
    // reordered after the seq store. Redundant on x86 but correct
    // for weakly-ordered ISAs.
    asm volatile("" ::: "memory");
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
    asm volatile("" ::: "memory");

    MemCpy(dst, self.data, self.dataSize);

    // Compiler barrier to prevent reordering of the seq load before the copy
    asm volatile("" ::: "memory");

    uint32_t after = AtomicLoad32(self.seq);
    return before == after;
}

} // namespace ShmChannel

} // namespace nyla
