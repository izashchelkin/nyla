#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"

namespace nyla
{

struct shm_channel;

namespace ShmChannel
{

// Seqlock-protected unidirectional shared-memory channel.
//
// Layout in shared memory:
//   [uint32_t seq][uint32_t _pad][dataSize bytes of user data]
//
// Writer: BeginWrite() increments seq (odd = write in progress),
//         caller fills data region, EndWrite() increments seq (even = done).
// Reader: TryRead() copies data; returns false if seq was odd or changed
//         during copy — caller retries next frame.

auto API CreateWriter(const char *name, uint64_t dataSize, region_alloc &alloc) -> shm_channel *;
auto API OpenReader(const char *name, uint64_t dataSize, region_alloc &alloc) -> shm_channel *;
void API Close(shm_channel &self);

// Returns writable pointer to the data region (after the 8-byte seqlock header).
// Must be paired with EndWrite(). Only valid for writer channels.
[[nodiscard]] auto API BeginWrite(shm_channel &self) -> void *;
void API EndWrite(shm_channel &self);

// Copies dataSize bytes into dst. Returns false if a write was in progress
// (seq odd or changed during copy) — caller retries next frame.
[[nodiscard]] auto API TryRead(shm_channel &self, void *dst) -> bool;

} // namespace ShmChannel

} // namespace nyla
