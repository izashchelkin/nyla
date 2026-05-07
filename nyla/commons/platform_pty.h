#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

struct platform_pty;

struct platform_pty_spawn_desc
{
    byteview shellPath; // executable to spawn (e.g. "/bin/bash"); on Windows treated as command line if shellPath only
    uint32_t cols;
    uint32_t rows;
};

namespace PlatformPty
{

auto API Create(region_alloc &alloc, const platform_pty_spawn_desc &desc) -> platform_pty *;
void API Destroy(platform_pty &self);

// Non-blocking. Returns bytes read into out (0 if nothing available). out must have capacity.
auto API Read(platform_pty &self, span<uint8_t> out) -> uint32_t;

// Writes bytes to the child stdin. Best-effort; returns bytes written.
auto API Write(platform_pty &self, byteview bytes) -> uint32_t;

void API Resize(platform_pty &self, uint32_t cols, uint32_t rows);

// True while child process is still running.
auto API IsAlive(platform_pty &self) -> bool;

} // namespace PlatformPty

} // namespace nyla
