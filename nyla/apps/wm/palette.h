#pragma once

#include <cstdint>

namespace nyla
{

enum class Color : uint32_t
{
    KNone = 0x000000,
    KActive = 0x95A3B3,
    KReserved0 = 0xC0FF1F,
    KActiveFollow = 0x84DCC6,
    KReserved1 = 0x5E1FFF,
};

} // namespace nyla
