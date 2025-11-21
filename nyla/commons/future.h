#include <future>

namespace nyla {

template <typename T>
inline bool IsFutureReady(const std::future<T>& fut) {
  using namespace std::chrono_literals;
  return fut.wait_for(0ms) == std::future_status::ready;
}

}  // namespace nyla