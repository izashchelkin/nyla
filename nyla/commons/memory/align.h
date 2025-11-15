#include <cstdint>

namespace nyla {

inline uint32_t AlignUp(uint32_t n, uint32_t align) {
  return n + (align - (n % align));
}

}  // namespace nyla