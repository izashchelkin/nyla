#pragma once

#include <cstdint>

#include "nyla/commons/byteparser.h"
#include "nyla/commons/json_value.h"

namespace nyla
{

struct json_parser : byte_parser
{
    json_value *out;
    uint64_t outSize;
};

namespace JsonParser
{

INLINE auto Init(json_parser &self, byteview in, span<json_value> jsonStorage)
{
    ByteParser::Init(self, in.data, in.size);
    self.out = jsonStorage.data;
    self.outSize = jsonStorage.size;
}

auto ParseNext(json_parser &self) -> json_value *;

} // namespace JsonParser

} // namespace nyla