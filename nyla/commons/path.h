#pragma once

#include "nyla/commons/dllapi.h"
#include "nyla/commons/str.h"

namespace nyla
{

class NYLA_API RegionAlloc;

class NYLA_API Path
{
  public:
    [[nodiscard]]
    static constexpr auto IsSeparator(char ch) -> bool
    {
        return ch == '/' || ch == '\\';
    }

    auto Init(RegionAlloc *alloc) -> Path &;

    [[nodiscard]] auto CStr() const -> const char *;
    [[nodiscard]] auto GetStr() const -> Str;
    [[nodiscard]] auto EndsWith(Str path) const -> bool;

    auto Append(Str path) -> Path &;
    auto PopBack() -> Path &;
    auto SetExtension(Str ext) -> Path &;

  private:
    char *m_Base;
    RegionAlloc *m_Alloc;
};

} // namespace nyla