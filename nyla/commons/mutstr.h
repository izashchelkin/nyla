#pragma once

#include <cstdint>

#include "nyla/commons/region_alloc.h"
#include "nyla/commons/str.h"

namespace nyla
{

constexpr inline auto AsciiChrToUpper(uint32_t ch) -> uint32_t
{
    if (ch >= 'a' && ch <= 'z')
        return ch - ('a' - 'A');
    else
        return ch;
}

class MutStr
{
    using Iterator = char *;
    using ConstIterator = const char *;

  public:
    void Init(RegionAlloc &&outerAlloc)
    {
        m_Alloc = outerAlloc;
        m_Data = nullptr;
        m_Size = 0;
    }
    void Append(Str);

    [[nodiscard]]
    auto StartWith(MutStr prefix) const -> bool
    {
        return MemStartsWith(this->Data(), this->Size(), prefix.Data(), prefix.Size());
    }

    [[nodiscard]]
    auto StartWith(Str prefix) const -> bool
    {
        return MemStartsWith(this->Data(), this->Size(), prefix.Data(), prefix.Size());
    }

    [[nodiscard]]
    auto EndsWith(MutStr suffix) const -> bool
    {
        return MemEndsWith(this->Data(), this->Size(), suffix.Data(), suffix.Size());
    }

    [[nodiscard]]
    auto EndsWith(Str suffix) const -> bool
    {
        return MemEndsWith(this->Data(), this->Size(), suffix.Data(), suffix.Size());
    }

    [[nodiscard]]
    auto Size() const -> uint64_t
    {
        return m_Size;
    }

    [[nodiscard]]
    auto Data() const -> char *
    {
        return m_Data;
    }

    [[nodiscard]]
    auto StrView() const -> Str
    {
        return {Data(), Size()};
    }

    [[nodiscard]]
    auto begin() -> Iterator
    {
        return Data();
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
    auto end() -> Iterator
    {
        return Data() + Size();
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
    RegionAlloc m_Alloc;
    char *m_Data;
    uint64_t m_Size;
};

//

constexpr inline void AsciiStrToUpperInPlace(MutStr &s)
{
    for (char &c : s)
        c = static_cast<char>(AsciiChrToUpper(c));
}

} // namespace nyla