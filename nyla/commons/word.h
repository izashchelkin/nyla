#pragma once

#include <cstdint>

namespace nyla
{

inline constexpr auto DWord(const char str[4]) -> uint32_t
{
    return str[0] | str[1] << 8 | str[2] << 16 | str[3] << 24;
}

} // namespace nyla