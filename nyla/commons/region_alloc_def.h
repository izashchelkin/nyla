#pragma once

#include <cstdint>

namespace nyla
{

struct region_alloc
{
    uint8_t *at;
    uint8_t *begin;
    uint8_t *end;
    uint8_t *commitedEnd;
};

} // namespace nyla
