#pragma once

#include <cstdint>

#include "nyla/commons/macros.h"

namespace nyla
{

auto API GetMonotonicTimeMillis() -> uint64_t;
auto API GetMonotonicTimeMicros() -> uint64_t;
auto API GetMonotonicTimeNanos() -> uint64_t;

struct wall_clock_time
{
    int year;   // e.g. 2026
    int month;  // 1–12
    int day;    // 1–31
    int hour;   // 0–23
    int minute; // 0–59
    int second; // 0–59
};

auto API GetWallClockTime() -> wall_clock_time;

} // namespace nyla