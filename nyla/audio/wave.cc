#include "nyla/audio/wave.h"

#include <cstdint>
#include <cstring>
#include <span>

namespace nyla
{

namespace
{

#pragma pack(push, 1)

struct WaveMasterChunk
{
    uint32_t chunkId;
    uint32_t chunkSize;
    uint32_t waveId;
};

struct WaveChunkHeader
{
    uint32_t chunkId;
    uint32_t chunkSize;
};

#pragma pack(pop)

constexpr auto Word32(const char str[4]) -> uint32_t
{
    return str[0] | str[1] << 8 | str[2] << 16 | str[3] << 24;
}

} // namespace

auto ParseWavFile(std::span<const std::byte> bytes) -> ParseWavFileResult
{
    ParseWavFileResult result{};

    const std::byte *p = bytes.data();

    {
        WaveMasterChunk masterChunk{};
        memcpy(&masterChunk, p, sizeof(masterChunk));
        p += sizeof(masterChunk);

        NYLA_ASSERT(masterChunk.chunkId == Word32("RIFF"));
        NYLA_ASSERT(masterChunk.waveId == Word32("WAVE"));
    }

    while (p != bytes.data() + bytes.size())
    {
        WaveChunkHeader header{};
        memcpy(&header, p, sizeof(header));

        switch (header.chunkId)
        {
        case Word32("fmt "): {
            result.fmt = (WaveFmtChunk *)p;

            NYLA_ASSERT(result.fmt->bitsPerSample == 16);

            break;
        }
        // case Word32("fact"): {
        //     break;
        // }
        case Word32("data"): {
            result.data = {p + sizeof(header), header.chunkSize};
            return result;
        }
        default: {
            NYLA_ASSERT(false);
            break;
        }
        }

        p += header.chunkSize + sizeof(header);
    }

    NYLA_ASSERT(false);
    return {};
}

} // namespace nyla