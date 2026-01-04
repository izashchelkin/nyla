#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "nyla/commons/assert.h"
#include "nyla/commons/handle.h"

namespace nyla
{

template <typename HandleType, typename DataType, uint32_t Size> class HandlePool
{
  public:
    static constexpr uint32_t kCapacity = Size;

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

    using value_type = Slot;
    using size_type = uint32_t;
    using difference_type = int32_t;
    using reference = Slot &;
    using const_reference = const Slot &;
    using pointer = Slot *;
    using const_pointer = const Slot *;
    using iterator = Slot *;
    using const_iterator = const Slot *;

    [[nodiscard]]
    auto data() const -> const_pointer
    {
        return m_Slots.data();
    }

    [[nodiscard]]
    auto data() -> pointer
    {
        return m_Slots.data();
    }

    [[nodiscard]]
    constexpr auto size() const -> size_type
    {
        return Size;
    }

    [[nodiscard]]
    constexpr auto max_size() const -> size_type
    {
        return Size;
    }

    [[nodiscard]]
    auto begin() -> iterator
    {
        return m_Slots.data();
    }

    [[nodiscard]]
    auto end() -> iterator
    {
        return m_Slots.data() + kCapacity;
    }

    [[nodiscard]]
    auto begin() const -> const_iterator
    {
        return m_Slots.data();
    }

    [[nodiscard]]
    auto end() const -> const_iterator
    {
        return m_Slots.data() + kCapacity;
    }

    [[nodiscard]]
    auto cbegin() const -> const_iterator
    {
        return m_Slots.data();
    }

    [[nodiscard]]
    auto cend() const -> const_iterator
    {
        return m_Slots.data() + kCapacity;
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

        for (uint32_t i = 0; i < Size; ++i)
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

    auto TryResolveSlot(HandleType handle) -> std::pair<bool, Slot *>
    {
        NYLA_ASSERT(handle.index < Size);

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
    std::array<Slot, static_cast<size_t>(Size)> m_Slots;
};

} // namespace nyla