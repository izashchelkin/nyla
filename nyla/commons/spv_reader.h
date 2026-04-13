/*
** Copyright: 2014-2024 The Khronos Group Inc.
** License: MIT
**
** MODIFICATIONS TO THIS FILE MAY MEAN IT NO LONGER ACCURATELY REFLECTS
** KHRONOS STANDARDS. THE UNMODIFIED, NORMATIVE VERSIONS OF KHRONOS
** SPECIFICATIONS AND HEADER INFORMATION ARE LOCATED AT
** https://www.khronos.org/registry/
*/

#pragma once

#include <cstdint>

#include "nyla/commons/fmt.h"
#include "nyla/commons/math.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/span.h"

namespace nyla
{

struct spv_shader_header
{
    uint32_t version;
    uint32_t generator;
    uint32_t bound;
};

namespace SpvReader
{

constexpr inline uint32_t kMagicNumber = 0x07230203;
constexpr inline uint32_t kOpCodeMask = 0xFFFF;
constexpr inline uint32_t kWordCountShift = 16;
constexpr inline uint32_t kNop = 1 << kWordCountShift;

[[nodiscard]]
INLINE auto ReadWord(span<uint32_t> &data) -> uint32_t &
{
    uint32_t &ret = Span::Front(data);
    data = Span::SubSpan(data, 1);
    return ret;
}

[[nodiscard]]
INLINE auto ReadString(span<uint32_t> &data) -> byteview
{
    uint8_t *ptr = (uint8_t *)data.data;
    uint64_t byteLen = CStrLen(ptr, data.size) + 1;

    uint64_t wordLen = CeilDiv(byteLen, 4);
    data = Span::SubSpan(data, wordLen);

    return byteview{ptr, byteLen};
}

[[nodiscard]]
INLINE auto VersionMajor(uint32_t version) -> uint8_t
{
    return (version >> 16) & 0xFF;
}

[[nodiscard]]
INLINE auto VersionMinor(uint32_t version) -> uint8_t
{
    return (version >> 8) & 0xFF;
}

[[nodiscard]]
INLINE auto ParseOp(uint32_t word) -> uint16_t
{
    return word & kOpCodeMask;
}

[[nodiscard]]
INLINE auto ParseWordCount(uint32_t word) -> uint16_t
{
    return word >> kWordCountShift;
}

// [[nodiscard]]
INLINE auto ReadHeader(span<uint32_t> &data) -> spv_shader_header
{
    NYLA_ASSERT(data.size >= 5);
    NYLA_ASSERT(data[0] == kMagicNumber);

    uint32_t version = ReadWord(data);
    uint32_t generator = ReadWord(data);
    uint32_t bound = ReadWord(data);
    uint32_t reserved = ReadWord(data);
    NYLA_ASSERT(reserved == 0);

    return spv_shader_header{
        .version = version,
        .generator = generator,
        .bound = bound,
    };
}

[[nodiscard]]
INLINE auto ReadOpWithOperands(span<uint32_t> &data) -> span<uint32_t>
{
    uint32_t wordCount = ParseWordCount(Span::Front(data));
    data = Span::SubSpan(data, wordCount);
    return span{data.data, wordCount};
}

INLINE void MakeNop(span<uint32_t> &data)
{
    MemSet(data.data, ParseWordCount(Span::Front(data)), kNop);
}

} // namespace SpvReader

} // namespace nyla