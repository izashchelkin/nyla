#include "nyla/commons/fmt.h"

#include <cstdarg>
#include <cstdint>

#include "nyla/commons/byteparser.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/word.h"

namespace nyla
{

namespace
{

auto U64ToBuffer(uint8_t *buf, uint64_t val) -> uint32_t
{
    if (val == 0)
    {
        buf[0] = '0';
        return 1;
    }

    uint8_t temp[20]; // Max digits for u64
    uint32_t i = 0;
    while (val > 0)
    {
        temp[i++] = (uint8_t)('0' + (val % 10));
        val /= 10;
    }

    uint32_t len = i;
    for (uint32_t j = 0; j < len; ++j)
    {
        buf[j] = temp[len - 1 - j];
    }
    return len;
}

auto S64ToBuffer(uint8_t *buf, int64_t val) -> uint32_t
{
    if (val < 0)
    {
        buf[0] = '-';
        return U64ToBuffer(buf + 1, (uint64_t)-val) + 1;
    }
    return U64ToBuffer(buf, (uint64_t)val);
}

void WriteFmt(auto &&consumer, byteview fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    byte_parser parser;
    ByteParser::Init(parser, fmt.data, fmt.size);

    while (ByteParser::HasNext(parser))
    {
        uint8_t ch = ByteParser::Read(parser);

        switch (ch)
        {
        case '%': {
            uint8_t ch1 = ByteParser::ReadOrDefault(parser, '\0');

            switch (ch1)
            {

            case '.': {
                uint8_t ch2 = ByteParser::ReadOrDefault(parser, '\0');
                uint8_t ch3 = ByteParser::ReadOrDefault(parser, '\0');

                switch ((uint32_t)(ch2 | (ch3 << 8)))
                {
                case DWord("*s\0\0"): {
                    uint64_t len = va_arg(args, uint64_t);
                    const uint8_t *str = va_arg(args, const uint8_t *);
                    consumer(str, (uint32_t)len);
                    break;
                }

                default: {
                    NYLA_ASSERT(false);
                    break;
                }
                }

                break;
            }

            case 's': {
                const uint8_t *s = va_arg(args, const uint8_t *);
                consumer(s, static_cast<uint32_t>(CStrLen(s)));
                break;
            }
            case 'd': {
                uint8_t buf[32];
                uint32_t len = S64ToBuffer(buf, va_arg(args, int));
                consumer(buf, len);
                break;
            }
            case 'u': {
                uint8_t buf[32];
                uint32_t len = U64ToBuffer(buf, va_arg(args, uint32_t));
                consumer(buf, len);
                break;
            }
            case 'p': {
                consumer((uint8_t *)"0x[pointer]", 11);
                break;
            }
            case '%': {
                consumer((uint8_t *)"%", 1);
                break;
            }

            default:
                NYLA_ASSERT(false);
                break;
            }

            break;
        }

        default: {
            const uint8_t *start = &ByteParser::Peek(parser) - 1;
            while (ByteParser::HasNext(parser) && ByteParser::Peek(parser) != '%')
                ByteParser::Advance(parser);

            consumer(start, (&ByteParser::Peek(parser) - start));
            break;
        }
        }
    }

    consumer((uint8_t *)"\n", 1);

    va_end(args);
}

void BufferWriteFmt(auto &&consumer, span<uint8_t> buffer, byteview fmt, ...)
{
    uint64_t bufferUsed = 0;

    va_list args;
    va_start(args, fmt);

    WriteFmt(
        [&](const uint8_t *data, uint32_t size) -> void {
            if (bufferUsed + size > buffer.size)
            {
                if (bufferUsed + size > buffer.size * 3 / 2)
                {
                    consumer(buffer.data, bufferUsed);
                    consumer(data, size);
                    bufferUsed = 0;
                }
                else
                {
                    uint64_t remainingInBuffer = buffer.size - bufferUsed;
                    MemCpy(buffer.data + bufferUsed, data, remainingInBuffer);
                    consumer(buffer.data, bufferUsed);

                    bufferUsed = size - remainingInBuffer;
                    MemCpy(buffer.data, data + remainingInBuffer, bufferUsed);
                }
            }
            else
            {
                MemCpy(buffer.data + bufferUsed, data, size);

                if (bufferUsed + size == buffer.size)
                {
                    consumer(buffer.data, bufferUsed);
                    bufferUsed = 0;
                }
            }
        },
        fmt, args);

    if (bufferUsed > 0)
        consumer(buffer.data, bufferUsed);

    va_end(args);
}

} // namespace

void NYLA_API StringWriteFmt(uint8_t *out, uint64_t outSize, byteview fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    uint64_t outWritten = 0;

    WriteFmt(
        [&](const uint8_t *data, uint64_t size) -> void {
            NYLA_ASSERT(outWritten + size < outSize);
            MemCpy(out + outSize, data, size);
            outSize += size;
        },
        fmt, args);

    va_end(args);
}

void NYLA_API FileWriteFmt(FileHandle handle, span<uint8_t> buffer, byteview fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    BufferWriteFmt(
        [handle](const uint8_t *data, uint64_t size) -> void { Platform::FileWrite(handle, (uint32_t)size, data); },
        buffer, fmt, args);

    va_end(args);
}

} // namespace nyla