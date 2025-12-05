#pragma once

#include <optional>

namespace nyla
{

template <typename T> auto Unown(T *p) -> std::optional<T>
{
    if (!p)
        return {};
    T &&ret = std::move(*p);
    free(p);
    return ret;
}

} // namespace nyla