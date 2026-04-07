#pragma once

#include <cstdarg>
#include <cstdint>

#include "nyla/commons/array.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/span.h"

namespace nyla
{

void NYLA_API StringWriteFmt(char *out, uint64_t outSize, byteview fmt, ...);

template <Plain T, uint64_t Capacity> struct inline_vec
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

template <uint64_t Capacity> using inline_string = inline_vec<uint8_t, Capacity>;

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

namespace InlineString
{

template <uint64_t Capacity> INLINE void AppendSuffix(inline_string<Capacity> &self, byteview suffix)
{
    InlineVec::Append(self, suffix);
    if (self.size < Capacity)
        NYLA_DASSERT((self.data + self.size) == '\0');
}

template <uint64_t Capacity> INLINE void RemoveSuffix(inline_string<Capacity> &self, uint64_t suffixLen)
{
    NYLA_DASSERT(suffixLen <= self.size);
    self.size -= suffixLen;
    self.data + self.size = '\0';
}

template <uint64_t Capacity> [[nodiscard]] INLINE bool TryRemoveSuffix(inline_string<Capacity> &self, byteview suffix)
{
    if (MemEndsWith(self.data, self.size, suffix.data, suffix.size))
    {
        RemoveSuffix(self, suffix.size);
        return true;
    }
    else
        return false;
}

template <uint64_t Capacity> void AsciiToUpper(inline_string<Capacity> &self)
{
    for (uint32_t i = 0; i < self.size; ++i)
    {
        char &ch = self[i];
        if (ch >= 'a' && ch <= 'z')
            ch = ch - ('a' - 'A');
        else
            ch = ch;
    }
}

template <uint64_t Capacity>
[[nodiscard]] INLINE auto WriteFmt(inline_string<Capacity> &out, byteview fmt, ...) -> byteview
{
    va_list args;
    va_start(args, fmt);
    StringWriteFmt(out.data + out.size, Capacity - out.size, fmt, args);
    va_end(args);
    return out;
}

}; // namespace InlineString

} // namespace nyla