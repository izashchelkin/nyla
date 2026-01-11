#pragma once

#include <cstdint>

namespace nyla
{

constexpr auto operator""_KiB(uint64_t v) -> uint64_t
{
    return v * (1 << 10);
}

constexpr auto operator""_MiB(uint64_t v) -> uint64_t
{
    return v * (1 << 20);
}

constexpr auto operator""_GiB(uint64_t v) -> uint64_t
{
    return v * (1 << 30);
}

} // namespace nyla