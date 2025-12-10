#include <string_view>
#define SPV_ENABLE_UTILITY_CODE

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "nyla/commons/logging/init.h"
#include "nyla/commons/os/readfile.h"

#include <cstddef>
#include <cstdint>
#include <span>

#include <spirv/unified1/spirv.hpp>

namespace nyla
{

namespace spirview_internal
{

struct SpirvDecoderState
{
};

}; // namespace spirview_internal

void DecodeSpirvWord()
{
}

auto FormatWord(uint32_t word)
{
    return std::format("0x{:08x}", word);
}

auto HasByte(uint32_t word, std::byte byte) -> bool
{
    if ((word & 0x000000FF) == uint32_t(byte))
        return true;
    if ((word & 0x0000FF00) == uint32_t(byte))
        return true;
    if ((word & 0x00FF0000) == uint32_t(byte))
        return true;
    if ((word & 0xFF000000) == uint32_t(byte))
        return true;
    return false;
}

auto ParseStringLiteral(std::span<const uint32_t> in, uint32_t &iin, std::span<uint32_t> out) -> bool
{
    uint32_t iout = 0;
    for (;;)
    {
        if (iin >= in.size())
            return false;
        if (iout >= out.size())
            return false;

        out[iout++] = in[iin];
        if (HasByte(in[iin], std::byte(0)))
            return true;

        ++iin;
    }

    return false;
}

struct SpirvParserState
{
    struct IdMetadata
    {
        bool exists;
    };

    std::array<IdMetadata, 256> ids;
};

void ProcessOp(spv::Op op, std::span<const uint32_t> args)
{
    LOG(INFO) << OpToString(op) << " " << args.size();

    uint32_t i = 0;

    switch (op)
    {
    case spv::OpMemoryModel: {
        auto addressingModel = spv::AddressingModel(args[i++]);
        auto memoryModel = spv::MemoryModel(args[i++]);

        LOG(INFO) << "    " << spv::AddressingModelToString(addressingModel) << " "
                  << spv::MemoryModelToString(memoryModel);
        break;
    }

    case spv::OpEntryPoint: {
        auto executionModel = spv::ExecutionModel(args[i++]);
        auto entryPointId = args[i++];

        std::array<uint32_t, 16> name;
        CHECK(ParseStringLiteral(args, i, name));

        LOG(INFO) << "    " << spv::ExecutionModelToString(executionModel) << " " << entryPointId << " "
                  << (const char *)name.data();

        break;
    }

    default: {
        break;
    }
    }
}

void Reflect(std::span<const uint32_t> spirv)
{
    if (spirv[0] != spv::MagicNumber)
    {
        LOG(ERROR) << "invalid spirv magic";
        return;
    }

    const uint32_t headerVersionMinor = (spirv[1] >> 8) & 0xFF;
    const uint32_t headerVersionMajor = (spirv[1] >> 16) & 0xFF;

    const uint32_t headerGeneratorMagic = spirv[2];
    const uint32_t headerIdBound = spirv[3];

    LOG(INFO) << "Version: " << FormatWord(spirv[1]) << " " << headerVersionMajor << "." << headerVersionMinor;
    LOG(INFO) << "Generator Magic: " << FormatWord(headerGeneratorMagic);
    LOG(INFO) << "Bound: " << headerIdBound;
    LOG(INFO) << "Words: " << spirv.size();
    LOG(INFO);

    for (uint32_t i = 5; i < spirv.size();)
    {
        const uint32_t word = spirv[i];

        const auto op = spv::Op(word & spv::OpCodeMask);
        const uint16_t wordCount = word >> spv::WordCountShift;
        CHECK(wordCount);

        ProcessOp(op, {spirv.data() + i + 1, uint16_t((wordCount - 1) * 4)});
        i += wordCount;
    }
}

namespace
{

auto Main() -> int
{
    LoggingInit();

    std::vector<std::byte> spirvBytes = ReadFile("nyla/apps/breakout/shaders/build/world.vs.hlsl.spv");
    if (spirvBytes.size() % 4)
    {
        LOG(ERROR) << "invalid spirv";
        return 1;
    }

    std::span<const uint32_t> spirvWords = {reinterpret_cast<uint32_t *>(spirvBytes.data()), spirvBytes.size() / 4};
    Reflect(spirvWords);

    return 0;
}

} // namespace

} // namespace nyla

auto main() -> int
{
    return nyla::Main();
}