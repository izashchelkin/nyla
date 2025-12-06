#pragma once

#include <cstdint>

namespace nyla
{

auto GetMonotonicTimeMillis() -> uint64_t;
auto GetMonotonicTimeMicros() -> uint64_t;
auto GetMonotonicTimeNanos() -> uint64_t;

} // namespace nyla