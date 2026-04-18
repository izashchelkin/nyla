#pragma once

#include <cstdint>
#include <type_traits>

#include "nyla/commons/fmt.h"
#include "nyla/commons/inline_vec_def.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/span.h"

namespace nyla
{

template <is_plain T, uint64_t Capacity>
[[nodiscard]]
auto inline_vec<T, Capacity>::operator[](uint64_t i) -> T &
{
    DASSERT(i < size);
    return data[i];
}

template <is_plain T, uint64_t Capacity>
[[nodiscard]]
auto inline_vec<T, Capacity>::operator[](uint64_t i) const -> const T &
{
    DASSERT(i < size);
    return data[i];
}

namespace InlineVec
{

template <typename T, uint64_t VecCapacity>
[[nodiscard]]
INLINE auto Capacity(inline_vec<T, VecCapacity> &self) -> uint64_t
{
    return VecCapacity;
}

template <typename T, uint64_t Capacity>
[[nodiscard]]
INLINE auto Size(inline_vec<T, Capacity> &self) -> uint64_t
{
    return self.size;
}

template <typename T, uint64_t Capacity>
[[nodiscard]]
INLINE auto DataPtr(inline_vec<T, Capacity> &self) -> T *
{
    return self.data.data;
}

template <typename T, uint64_t Capacity>
[[nodiscard]]
INLINE auto Front(const inline_vec<T, Capacity> &self) -> const T &
{
    DASSERT(self.size);
    return self[0];
}

template <typename T, uint64_t Capacity>
[[nodiscard]]
INLINE auto Back(const inline_vec<T, Capacity> &self) -> const T &
{
    DASSERT(self.size);
    return self[self.size - 1];
}

template <typename T, uint64_t Capacity> INLINE void Resize(inline_vec<T, Capacity> &self, uint64_t newSize)
{
    DASSERT(newSize <= Capacity);
    self.size = newSize;
}

template <typename T, uint64_t Capacity> INLINE void Clear(inline_vec<T, Capacity> &self)
{
    self.size = 0;
}

template <typename T, uint64_t Capacity> INLINE auto Append(inline_vec<T, Capacity> &self) -> T &
{
    DASSERT(self.size < Capacity);
    T *p = self.data + self.size++;
    return *p;
}

template <typename T, typename D, uint64_t Capacity>
INLINE auto Append(inline_vec<T, Capacity> &self, const D &data) -> T &
    requires(std::is_convertible_v<D, T>)
{
    T &ret = Append(self);
    ret = data;
    return ret;
}

template <typename T, uint64_t Capacity> INLINE auto Append(inline_vec<T, Capacity> &self, span<const T> data) -> T &
{
    DASSERT(self.size + data.size < Capacity);
    T &ret = Back(self);
    MemCpy(&ret, data.data, Span::SizeBytes(data));
    self.size += data.size;
    return ret;
}

template <typename T, uint64_t Capacity> INLINE auto PopBack(inline_vec<T, Capacity> &self) -> T
{
    T ret = Back(self);
    --self.size;
    return ret;
}

}; // namespace InlineVec

} // namespace nyla