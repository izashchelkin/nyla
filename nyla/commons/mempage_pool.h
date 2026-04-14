#pragma once

#include <cstdint>

#include "nyla/commons/byteliterals.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace MemPagePool
{

constexpr inline uint64_t kPoolSize = 256_GiB;
constexpr inline uint64_t kChunkSize = 256_MiB;
constexpr inline uint64_t kNumChunks = kPoolSize / kChunkSize;

auto API AcquireChunk() -> span<uint8_t>;
void API ReleaseChunk(void *p);

} // namespace MemPagePool

} // namespace nyla