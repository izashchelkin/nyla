#include "nyla/commons/stringparser.h"

#include <cstdlib>

namespace nyla
{

namespace StringParser
{

auto ParseDoubleFromBytes(const uint8_t *data, uint64_t size) -> double
{
    char buf[1024];
    ASSERT(size < sizeof(buf));

    for (uint64_t i = 0; i < size; ++i)
        buf[i] = (char)data[i];
    buf[size] = '\0';

    return ::strtod(buf, nullptr);
}

} // namespace StringParser

} // namespace nyla
