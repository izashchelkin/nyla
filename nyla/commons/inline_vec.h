#pragma once

#include <cstdint>

#include "nyla/commons/array.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/span.h"

namespace nyla
{

template <is_plain T, uint64_t Capacity> struct inline_vec
{
    array<T, sizeof(T) * Capacity> data;
    uint64_t size;

    [[nodiscard]]
    auto operator[](uint64_t i) -> T &
    {
        NYLA_DASSERT(i < size);
        return data[i];
    }

    [[nodiscard]]
    auto operator[](uint64_t i) const -> const T &
    {
        NYLA_DASSERT(i < size);
        return data[i];
    }

    [[nodiscard]]
    auto begin() -> T *
    {
        return data;
    }

    [[nodiscard]]
    auto begin() const -> const T *
    {
        return data;
    }

    [[nodiscard]]
    auto cbegin() const -> const T *
    {
        return data;
    }

    [[nodiscard]]
    auto end() -> T *
    {
        return data + Capacity;
    }

    [[nodiscard]]
    auto end() const -> const T *
    {
        return data + Capacity;
    }

    [[nodiscard]]
    auto cend() const -> const T *
    {
        return data + Capacity;
    }

    operator span<T>()
    {
        return span<T>{data, size};
    }

    operator span<const T>() const
    {
        return span<const T>{data, size};
    }
};

namespace InlineVec
{

template <typename T, uint64_t Capacity>
[[nodiscard]]
INLINE auto Front(const inline_vec<T, Capacity> &self) -> T &
{
    NYLA_DASSERT(self.size);
    return self[0];
}

template <typename T, uint64_t Capacity>
[[nodiscard]]
INLINE auto Back(const inline_vec<T, Capacity> &self) -> T &
{
    NYLA_DASSERT(self.size);
    return self[self.size - 1];
}

template <typename T, uint64_t Capacity> INLINE void Resize(inline_vec<T, Capacity> &self, uint64_t newSize)
{
    NYLA_DASSERT(newSize <= Capacity);
    self.size = newSize;
}

template <typename T, uint64_t Capacity> INLINE void Clear(inline_vec<T, Capacity> &self)
{
    self.size = 0;
}

template <typename T, uint64_t Capacity> INLINE auto Append(inline_vec<T, Capacity> &self) -> T &
{
    NYLA_DASSERT(self.size < Capacity);
    T *p = self.data + self.size++;
    return *p;
}

template <typename T, uint64_t Capacity> INLINE auto Append(inline_vec<T, Capacity> &self, const T &data) -> T &
{
    T &ret = Append(self);
    ret = data;
    return ret;
}

template <typename T, uint64_t Capacity> INLINE auto Append(inline_vec<T, Capacity> &self, span<const T> data) -> T &
{
    NYLA_DASSERT(self.size + data.size < Capacity);
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

template <typename T, uint64_t Capacity> INLINE auto Erase(inline_vec<T, Capacity> &self, const T *pos) -> T *
{
    NYLA_DASSERT(pos >= self.begin() && pos < self.end());
    return Erase(pos, pos + 1);
}

template <typename T, uint64_t Capacity> INLINE auto Erase(inline_vec<T, Capacity> &self, T *first, T *last) -> T *
{
    if (first == last)
        return first;

    uint64_t numRemoved = last - first;
    uint64_t numToMove = self.end() - last;

    if (numToMove > 0)
        MemMove(first, last, numToMove * sizeof(T));

    self.size -= numRemoved;
    return first;
}

}; // namespace InlineVec

} // namespace nyla