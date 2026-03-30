#pragma once

#include "nyla/commons/dllapi.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"

namespace nyla
{

struct NYLA_API Path
{
    RegionAlloc m_Alloc;

    [[nodiscard]]
    static constexpr auto IsSeparator(char ch) -> bool
    {
        return ch == '/' || ch == '\\';
    }

    auto Init(RegionAlloc alloc) -> Path &;

    [[nodiscard]] auto CStr() const -> const char *;
    [[nodiscard]] auto GetStr() const -> Str;
    [[nodiscard]] auto EndsWith(Str path) const -> bool;

    auto Append(Str path) -> Path &;
    auto PopBack() -> Path &;
    auto SetExtension(Str ext) -> Path &;
};

inline auto CreatePath(RegionAlloc alloc) -> Path
{
    Path path;
    path.Init(alloc);
    return path;
}

inline auto CreatePath(RegionAlloc alloc, Str path) -> Path
{
    return CreatePath(alloc).Append(path);
}

inline auto ClonePath(RegionAlloc alloc, const Path &path) -> Path
{
    return CreatePath(alloc);
}

} // namespace nyla