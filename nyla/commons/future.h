#pragma once

#include <future>

namespace nyla
{

template <typename T> inline auto IsFutureReady(const std::future<T> &fut) -> bool
{
    if (!fut.valid())
        return true;

    using namespace std::chrono_literals;
    return fut.wait_for(0ms) == std::future_status::ready;
}

} // namespace nyla