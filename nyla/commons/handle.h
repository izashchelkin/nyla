#pragma once

#include <cstdint>

namespace nyla
{

struct Handle
{
    uint32_t gen;
    uint32_t index;
};

inline auto HandleIsSet(Handle handle) -> bool
{
    return handle.gen;
}

} // namespace nyla