#pragma once

#include <cstdint>

#include "nyla/commons/array.h"
#include "nyla/commons/handle.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/tuple.h"

namespace nyla
{

template <typename HandleType, typename DataType, uint64_t SlotCapacity> class HandlePool
{
  public:
    struct Slot
    {
        DataType data;
        uint32_t gen;
        bool used;

        void Free()
        {
            this->~Slot();
            used = false;
        }
    };

    [[nodiscard]]
    auto Data() const -> const Slot *
    {
        return m_Slots.data();
    }

    [[nodiscard]]
    auto Data() -> Slot *
    {
        return m_Slots.data();
    }

    [[nodiscard]]
    constexpr auto Size() const -> uint64_t
    {
        return SlotCapacity;
    }

    [[nodiscard]]
    constexpr auto MaxSize() const -> uint64_t
    {
        return SlotCapacity;
    }

    [[nodiscard]]
    auto begin() -> Slot *
    {
        return m_Slots.data();
    }

    [[nodiscard]]
    auto begin() const -> const Slot *
    {
        return m_Slots.data();
    }

    [[nodiscard]]
    auto cbegin() const -> const Slot *
    {
        return m_Slots.data();
    }

    [[nodiscard]]
    auto end() -> Slot *
    {
        return m_Slots.data() + MaxSize();
    }

    [[nodiscard]]
    auto end() const -> const Slot *
    {
        return m_Slots.data() + MaxSize();
    }

    [[nodiscard]]
    auto cend() const -> const Slot *
    {
        return m_Slots.data() + MaxSize();
    }

    auto operator[](uint32_t i) -> Slot &
    {
        return m_Slots[i];
    }

    auto operator[](uint32_t i) const -> const Slot &
    {
        return m_Slots[i];
    }

    //

    auto Acquire(const DataType &data) -> HandleType
    {
        HandleType ret{};

        for (uint32_t i = 0; i < MaxSize(); ++i)
        {
            Slot &slot = m_Slots[i];
            if (slot.used)
                continue;

            ++slot.gen;
            slot.used = true;
            slot.data = data;

            return static_cast<HandleType>(Handle{
                .gen = slot.gen,
                .index = i,
            });
        }

        NYLA_ASSERT(false);
        return {};
    }

    auto TryResolveSlot(HandleType handle) -> Pair<bool, Slot *>
    {
        NYLA_ASSERT(handle.index < MaxSize());

        if (!handle.gen)
            return {false, nullptr};

        Slot *slot = &m_Slots[handle.index];
        if (!slot->used)
            return {false, nullptr};
        if (handle.gen != slot->gen)
            return {false, nullptr};

        return {true, slot};
    }

    auto ResolveSlot(HandleType handle) -> Slot &
    {
        auto [ok, slot] = TryResolveSlot(handle);
        NYLA_ASSERT(ok);
        return *slot;
    }

    auto ResolveData(HandleType handle) -> DataType &
    {
        return ResolveSlot(handle).data;
    }

    auto ResolveData(HandleType handle) const -> const DataType &
    {
        return ResolveSlot(handle).data;
    }

    auto ReleaseData(HandleType handle) -> DataType
    {
        Slot &slot = ResolveSlot(handle);
        slot.Free();
        return slot.data;
    }

  private:
    Array<Slot, SlotCapacity> m_Slots;
};

} // namespace nyla