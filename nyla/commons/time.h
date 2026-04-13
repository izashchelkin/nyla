#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"

namespace nyla
{

auto API GetMonotonicTimeMillis() -> uint64_t;
auto API GetMonotonicTimeMicros() -> uint64_t;
auto API GetMonotonicTimeNanos() -> uint64_t;

} // namespace nyla
