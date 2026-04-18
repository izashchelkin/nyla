#pragma once

#include <cstdint>

#include "nyla/commons/array_def.h"
#include "nyla/commons/concepts.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/macros.h"

namespace nyla
{

template <is_plain T, uint64_t Size>
[[nodiscard]]
INLINE auto array<T, Size>::operator[](uint64_t i) -> T &
{
    DASSERT(i < Size);
    return data[i];
}

template <is_plain T, uint64_t Size>
[[nodiscard]]
auto array<T, Size>::operator[](uint64_t i) const -> const T &
{
    DASSERT(i < Size);
    return data[i];
}

namespace Array
{

template <typename T, uint64_t ArraySize>
[[nodiscard]]
constexpr auto Size(const array<T, ArraySize> &self) -> uint64_t
{
    return ArraySize;
}

template <typename T, uint64_t Size>
[[nodiscard]]
INLINE auto Front(const array<T, Size> &self) -> T &
{
    return self[0];
}

template <typename T, uint64_t Size>
[[nodiscard]]
INLINE auto Back(const array<T, Size> &self) -> T &
{
    return self[Size - 1];
}

} // namespace Array

} // namespace nyla