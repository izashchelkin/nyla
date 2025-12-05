#pragma once

#include <cstddef>
#include <cstdint>

#include "absl/log/check.h"

namespace nyla
{

struct RhiHandle
{
    uint32_t gen;
    uint32_t index;
};

inline auto RhiHandleIsSet(RhiHandle handle) -> bool
{
    return handle.gen;
}

namespace rhi_internal
{

template <typename Handle, typename Data, size_t Size> struct RhiHandlePool
{
    struct Slot
    {
        Data data;
        uint32_t gen;
        bool used;
    };
    std::array<Slot, Size> slots;
};

template <typename Handle, typename Data, size_t Size>
inline auto RhiHandleAcquire(RhiHandlePool<Handle, Data, Size> &pool, Data data, bool allowIntern = false) -> Handle
{
    Handle retHandle{};

    for (uint32_t i = 0; i < Size; ++i)
    {
        auto &slot = pool.slots[i];
        if (slot.used)
        {
            CHECK(memcmp(&slot.data, &data, sizeof(slot.data)) || allowIntern);
            continue;
        }

        if (!RhiHandleIsSet(retHandle))
        {
            ++slot.gen;
            slot.used = true;
            slot.data = data;

            retHandle = static_cast<Handle>(RhiHandle{
                .gen = slot.gen,
                .index = i,
            });
        }
    }

    CHECK(RhiHandleIsSet(retHandle));
    return retHandle;
}

template <typename Handle, typename Data, size_t Size>
inline auto RhiHandleGetData(RhiHandlePool<Handle, Data, Size> &pool, Handle handle) -> Data &
{
    CHECK(handle.gen);
    CHECK_LT(handle.index, Size);

    auto &slot = pool.slots[handle.index];
    if (slot.used && handle.gen == slot.gen)
    {
        return slot.data;
    }

    CHECK(false);
}

template <typename Handle, typename Data, size_t Size>
inline auto RhiHandleRelease(RhiHandlePool<Handle, Data, Size> &pool, Handle handle) -> Data
{
    CHECK(handle.gen);
    CHECK_LT(handle.index, Size);

    auto &slot = pool.slots[handle.index];
    if (slot.used && handle.gen == slot.gen)
    {
        slot.used = false;
        return slot.data;
    }

    CHECK(false);
}

} // namespace rhi_internal

} // namespace nyla