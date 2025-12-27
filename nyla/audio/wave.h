#pragma once

#include <cstdint>
#include <span>

namespace nyla
{

struct WaveFmtChunk
{
    uint32_t chunkId;
    uint32_t chunkSize;
    uint16_t formatTag;
    uint16_t numChannels;
    uint32_t numSamplesPerSec;
    uint32_t numAvgBytesPerSec;
    uint16_t numBlockAlign;
    uint16_t bitsPerSample;
    uint16_t extensionSize;
    uint16_t validBitsPerSample;
    uint32_t channelMask;

    struct
    {
        uint64_t lo;
        uint64_t hi;
    } subFormat;
};

struct ParseWavFileResult
{
    WaveFmtChunk *fmt;
    std::span<const std::byte> data;

    auto GetSampleRate()
    {
        return fmt->numSamplesPerSec;
    }

    auto GetNumChannels()
    {
        return fmt->numChannels;
    }
};

auto ParseWavFile(std::span<const std::byte> bytes) -> ParseWavFileResult;

} // namespace nyla