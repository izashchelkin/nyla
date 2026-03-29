#pragma once

#include <cstdint>

#include "nyla/commons/mem.h"

namespace nyla
{

class Str
{
    using Iterator = char *;
    using ConstIterator = const char *;

  public:
    Str(const char *data, uint64_t size) : m_Data{data}, m_Size{size}
    {
    }

    Str() : Str{nullptr, 0ULL}
    {
    }

    explicit Str(const char *data) : Str(data, CStrLen(data))
    {
    }

    [[nodiscard]]
    auto StartWith(Str prefix) const -> bool
    {
        return MemStartsWith(this->Data(), this->Size(), prefix.Data(), prefix.Size());
    }

    [[nodiscard]]
    auto EndsWith(Str suffix) const -> bool
    {
        return MemEndsWith(this->Data(), this->Size(), suffix.Data(), suffix.Size());
    }

    [[nodiscard]]
    auto operator[](uint64_t i) const -> char
    {
        return m_Data[i];
    }

    [[nodiscard]]
    auto Empty() const -> bool
    {
        return m_Size == 0;
    }

    [[nodiscard]]
    auto Data() const -> const char *
    {
        return m_Data;
    }

    [[nodiscard]]
    auto Size() const -> uint64_t
    {
        return m_Size;
    }

    [[nodiscard]]
    auto begin() const -> ConstIterator
    {
        return Data();
    }

    [[nodiscard]]
    auto cbegin() const -> ConstIterator
    {
        return Data();
    }

    [[nodiscard]]
    auto end() const -> ConstIterator
    {
        return Data() + Size();
    }

    [[nodiscard]]
    auto cend() const -> ConstIterator
    {
        return Data() + Size();
    }

  private:
    const char *m_Data;
    uint64_t m_Size;
};

} // namespace nyla