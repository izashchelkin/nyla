#pragma once

#include <span>

namespace nyla
{

using CharView = std::span<const char>;

template <typename T> inline auto CharViewSpan(std::span<T> in) -> std::span<const char>
{
    return {
        reinterpret_cast<const char *>(in.data()),
        in.size() * sizeof(T),
    };
}

template <typename T> inline auto CharViewPtr(T *in) -> std::span<const char>
{
    return {
        reinterpret_cast<const char *>(in),
        sizeof(*in),
    };
}

} // namespace nyla