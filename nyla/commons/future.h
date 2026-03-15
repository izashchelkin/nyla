#pragma once

#include <future>

namespace nyla
{

template <typename T> inline auto IsFutureReady(const std::future<T> &fut) -> bool
{
    if (fut.valid())
        return fut.wait_for(0) == std::future_status::ready;
    else
        return true;
}

} // namespace nyla