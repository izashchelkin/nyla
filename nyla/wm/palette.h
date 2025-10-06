#pragma once

#include <cstdint>

namespace nyla {

enum class Color : uint32_t {
  kDefault = 0x000000,
  kActive = 0xFF5E1F,
  kReserved0 = 0xC0FF1F,
  kActiveFollow = 0x1FC0FF,
  kReserved1 = 0x5E1FFF,
};

}
