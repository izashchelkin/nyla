#pragma once

#include <span>

namespace nyla
{

using ByteView = std::span<const std::byte>;

template <typename T, size_t N> inline auto ByteViewSpan(std::span<T, N> in) -> ByteView
{
    return std::as_bytes(in);
}

template <typename T> inline auto ByteViewSpan(std::span<T> in) -> ByteView
{
    return std::as_bytes(in);
}

template <typename T> inline auto ByteViewPtr(const T *in) -> ByteView
{
    return std::as_bytes(std::span{in, 1});
}

} // namespace nyla