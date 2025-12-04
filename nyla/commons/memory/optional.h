#pragma once

#include <optional>

namespace nyla
{

template <typename T> std::optional<T> Unown(T *p)
{
    if (!p)
        return {};
    T &&ret = std::move(*p);
    free(p);
    return ret;
}

} // namespace nyla