#pragma once

#include <cstdint>

namespace nyla
{

constexpr auto operator""_KiB(unsigned long long v) -> uint64_t
{
    return static_cast<uint64_t>(v) * (1 << 10);
}

constexpr auto operator""_MiB(unsigned long long v) -> uint64_t
{
    return static_cast<uint64_t>(v) * (1 << 20);
}

constexpr auto operator""_GiB(unsigned long long v) -> uint64_t
{
    return static_cast<uint64_t>(v) * (1 << 30);
}

constexpr auto operator""_TiB(unsigned long long v) -> uint64_t
{
    return static_cast<uint64_t>(v) * (1ULL << 40);
}

} // namespace nyla