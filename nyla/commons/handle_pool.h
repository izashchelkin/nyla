#pragma once

#include <cstddef>
#include <cstdint>

#include "absl/log/check.h"
#include "nyla/commons/handle.h"

namespace nyla
{

template <typename T, typename Data, size_t Size> struct HandlePool
{
    struct Slot
    {
        Data data;
        uint32_t gen;
        bool used;
    };
    std::array<Slot, Size> slots;
};

template <typename T, typename Data, size_t Size>
inline auto HandleAcquire(HandlePool<T, Data, Size> &pool, Data data) -> T
{
    T retHandle{};

    for (uint32_t i = 0; i < Size; ++i)
    {
        auto &slot = pool.slots[i];
        if (slot.used)
        {
            CHECK(memcmp(&slot.data, &data, sizeof(slot.data)));
            continue;
        }

        if (!HandleIsSet(retHandle))
        {
            ++slot.gen;
            slot.used = true;
            slot.data = data;

            retHandle = static_cast<T>(Handle{
                .gen = slot.gen,
                .index = i,
            });
        }
    }

    CHECK(HandleIsSet(retHandle));
    return retHandle;
}

template <typename T, typename Data, size_t Size>
inline auto HandleGetData(HandlePool<T, Data, Size> &pool, T handle) -> Data &
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

template <typename T, typename Data, size_t Size>
inline auto HandleRelease(HandlePool<T, Data, Size> &pool, T handle) -> Data
{
    CHECK(handle.gen);
    CHECK_LT(handle.index, Size);

    auto &slot = pool.slots[handle.index];
    CHECK_EQ(handle.gen, slot.gen);
    CHECK(slot.used);

    slot.used = false;
    return slot.data;
}

} // namespace nyla