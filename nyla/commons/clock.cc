#include "nyla/commons/clock.h"

#include <cstdint>
#include <ctime>

namespace nyla {

uint64_t GetMonotonicTimeMillis() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 10e6;
}

}  // namespace nyla
