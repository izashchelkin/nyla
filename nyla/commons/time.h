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

struct deadline
{
    uint64_t target_ms; // 0 means never expires

    static auto Never() -> deadline
    {
        return {0};
    }
    static auto FromMillis(uint64_t ms) -> deadline
    {
        return {GetMonotonicTimeMillis() + ms};
    }

    auto IsActive() const -> bool
    {
        return target_ms != 0;
    }
    auto IsExpired() const -> bool
    {
        return IsActive() && GetMonotonicTimeMillis() >= target_ms;
    }
    auto RemainingMs() const -> uint64_t
    {
        if (!IsActive())
            return UINT64_MAX;
        uint64_t now = GetMonotonicTimeMillis();
        return (now >= target_ms) ? 0 : target_ms - now;
    }
};

} // namespace nyla
