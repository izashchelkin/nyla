#pragma once

#include <cstdint>

#include "nyla/commons/array_def.h"

namespace nyla
{

template <is_plain T, uint64_t Capacity> struct inline_queue
{
    uint64_t write;
    uint64_t read;
    array<T, Capacity> data;
};

namespace InlineQueue
{

template <typename T, uint64_t Capacity> INLINE auto Size(inline_queue<T, Capacity> &self) -> bool
{
    if (self.write >= self.read)
        return self.write - self.read;
    else
        return Capacity - (self.read - self.write);
}

template <typename T, uint64_t Capacity> INLINE auto IsEmpty(inline_queue<T, Capacity> &self) -> bool
{
    return self.write == self.read;
}

template <typename T, uint64_t Capacity> INLINE void Clear(inline_queue<T, Capacity> &self)
{
    self.read = 0;
    self.write = 0;
}

template <typename T, uint64_t Capacity> INLINE void Write(inline_queue<T, Capacity> &self, const T &data)
{
    self.data[self.write] = data;
    self.write = (self.write + 1) % Capacity;
}

template <typename T, uint64_t Capacity> INLINE auto Read(inline_queue<T, Capacity> &self) -> T
{
    T ret = self.data[self.read];
    self.read = (self.read + 1) % Capacity;
    return ret;
}

} // namespace InlineQueue

} // namespace nyla