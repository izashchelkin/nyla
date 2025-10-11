#pragma once

#include <cstdint>

namespace nyla {

enum class Color : uint32_t {
  kNone = 0x000000,
  kActive = 0x95A3B3,
  kReserved0 = 0xC0FF1F,
  kActiveFollow = 0x84DCC6,
  kReserved1 = 0x5E1FFF,
};

}
