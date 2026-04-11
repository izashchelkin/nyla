#pragma once

#include <cstdint>

#include "nyla/commons/byteparser.h"
#include "nyla/commons/json/json_value.h"

namespace nyla
{

struct json_parser : byte_parser
{
    json_value *out;
    uint64_t outSize;
};

namespace JsonParser
{

auto ParseNext(json_parser &self) -> json_value *;

}

} // namespace nyla