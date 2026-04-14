#pragma once

#include <cstdint>

#include "nyla/commons/array_def.h"
#include "nyla/commons/handle.h"
#include "nyla/commons/tuple.h"

namespace nyla
{

template <typename DataType> struct handle_slot
{
    DataType data;
    uint32_t gen;
    bool used;
};

template <is_handle HandleType, typename DataType, uint64_t Capacity>
struct handle_pool : array<handle_slot<DataType>, Capacity>
{
};

namespace HandlePool
{

template <is_handle HandleType, typename DataType, uint64_t PoolCapacity>
[[nodiscard]]
constexpr auto Capacity(const handle_pool<HandleType, DataType, PoolCapacity> &self) -> uint64_t
{
    return PoolCapacity;
}

template <is_handle HandleType, typename DataType, uint64_t Capacity>
[[nodiscard]]
auto Acquire(handle_pool<HandleType, DataType, Capacity> &self, const DataType &data) -> HandleType
{
    HandleType ret{};

    for (uint32_t i = 0; i < Capacity; ++i)
    {
        auto &slot = self[i];
        if (slot.used)
            continue;

        ++slot.gen;
        slot.used = true;
        slot.data = data;

        return HandleType{
            .gen = slot.gen,
            .index = i,
        };
    }

    ASSERT(false);
    return {};
}

template <is_handle HandleType, typename DataType, uint64_t Capacity>
[[nodiscard]]
auto TryResolveSlot(handle_pool<HandleType, DataType, Capacity> &self, HandleType handle)
    -> pair<bool, handle_slot<DataType> *>
{
    DASSERT(handle.index < Capacity);

    if (!handle.gen)
        return {false, nullptr};

    auto slot = self[handle.index];
    if (!slot.used)
        return {false, nullptr};
    if (handle.gen != slot->gen)
        return {false, nullptr};

    return {true, &slot};
}

template <is_handle HandleType, typename DataType, uint64_t Capacity>
[[nodiscard]] auto ResolveSlot(handle_pool<HandleType, DataType, Capacity> &self, HandleType handle)
    -> handle_slot<DataType> &
{
    auto [ok, slot] = TryResolveSlot(self, handle);
    ASSERT(ok);
    return *slot;
}

template <is_handle HandleType, typename DataType, uint64_t Capacity>
[[nodiscard]] auto ResolveData(handle_pool<HandleType, DataType, Capacity> &self, HandleType handle) -> DataType &
{
    return ResolveSlot(self, handle).data;
}

template <is_handle HandleType, typename DataType, uint64_t Capacity>
auto ResolveData(const handle_pool<HandleType, DataType, Capacity> &self, HandleType handle) -> const DataType &
{
    return ResolveSlot(self, handle).data;
}

template <is_handle HandleType, typename DataType, uint64_t Capacity>
auto ReleaseData(handle_pool<HandleType, DataType, Capacity> &self, HandleType handle) -> DataType
{
    auto &slot = ResolveSlot(self, handle);
    slot.used = false;
    return slot.data;
}

template <typename DataType> auto Free(const handle_slot<DataType> &slot) -> DataType
{
    slot.used = false;
    return slot.data;
}

} // namespace HandlePool

} // namespace nyla