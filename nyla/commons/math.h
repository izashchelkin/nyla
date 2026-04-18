#pragma once

#include <cstdint>

namespace nyla
{

namespace math
{

constexpr inline float pi = 3.141592653589793f;

}

constexpr auto CeilDiv(uint32_t a, uint32_t b) -> uint32_t
{
    return (a + b - 1) / b;
}

constexpr auto CeilDiv(uint64_t a, uint64_t b) -> uint64_t
{
    return (a + b - 1) / b;
}

} // namespace nyla