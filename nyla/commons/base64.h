#pragma once

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace Base64
{

INLINE auto Encode(region_alloc &alloc, byteview data) -> byteview
{
    static const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t outLen = 4 * ((data.size + 2) / 3);
    uint8_t *out = RegionAlloc::Alloc(alloc, outLen, 1);

    for (uint32_t i = 0, j = 0; i < data.size;)
    {
        uint32_t octet_a = i < data.size ? data.data[i++] : 0;
        uint32_t octet_b = i < data.size ? data.data[i++] : 0;
        uint32_t octet_c = i < data.size ? data.data[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        out[j++] = table[(triple >> 3 * 6) & 0x3F];
        out[j++] = table[(triple >> 2 * 6) & 0x3F];
        out[j++] = table[(triple >> 1 * 6) & 0x3F];
        out[j++] = table[(triple >> 0 * 6) & 0x3F];
    }

    const uint32_t mod = data.size % 3;
    if (mod > 0)
    {
        for (uint32_t i = 0; i < 3 - mod; ++i)
            out[outLen - 1 - i] = '=';
    }

    return {out, outLen};
}

INLINE auto Decode(region_alloc &alloc, byteview data) -> byteview
{
    if (data.size == 0)
        return {};

    static const uint8_t table[256] = {
        0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0,  0,  0,  0,  62, 0,  0,  0,  63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
        0,  0,  0,  0,  0,  0,  0,  0, 1, 2, 3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
        22, 23, 24, 25, 0,  0,  0,  0, 0, 0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44,
        45, 46, 47, 48, 49, 50, 51, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0, 0, 0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};

    uint32_t outLen = data.size / 4 * 3;
    if (data.size > 0 && data.data[data.size - 1] == '=')
        --outLen;
    if (data.size > 1 && data.data[data.size - 2] == '=')
        --outLen;

    uint8_t *out = RegionAlloc::Alloc(alloc, outLen, 1);
    for (uint32_t i = 0, j = 0; i < data.size;)
    {
        uint32_t sextet_a = data.data[i] == '=' ? 0 & i++ : table[data.data[i++]];
        uint32_t sextet_b = data.data[i] == '=' ? 0 & i++ : table[data.data[i++]];
        uint32_t sextet_c = data.data[i] == '=' ? 0 & i++ : table[data.data[i++]];
        uint32_t sextet_d = data.data[i] == '=' ? 0 & i++ : table[data.data[i++]];

        uint32_t triple = (sextet_a << 3 * 6) + (sextet_b << 2 * 6) + (sextet_c << 1 * 6) + (sextet_d << 0 * 6);

        if (j < outLen)
            out[j++] = (triple >> 2 * 8) & 0xFF;
        if (j < outLen)
            out[j++] = (triple >> 1 * 8) & 0xFF;
        if (j < outLen)
            out[j++] = (triple >> 0 * 8) & 0xFF;
    }

    return {out, outLen};
}

} // namespace Base64

} // namespace nyla
