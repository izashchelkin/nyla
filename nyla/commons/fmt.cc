#include "nyla/commons/fmt.h"
#include <cstdarg>
#include <cstdint>

#include "nyla/commons/byteparser.h"
#include "nyla/commons/dllapi.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/platform_base.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/word.h"

namespace nyla
{

namespace
{

auto U64ToBuffer(char *buf, uint64_t val) -> uint32_t
{
    if (val == 0)
    {
        buf[0] = '0';
        return 1;
    }

    char temp[20]; // Max digits for u64
    uint32_t i = 0;
    while (val > 0)
    {
        temp[i++] = (char)('0' + (val % 10));
        val /= 10;
    }

    uint32_t len = i;
    for (uint32_t j = 0; j < len; ++j)
    {
        buf[j] = temp[len - 1 - j];
    }
    return len;
}

auto S64ToBuffer(char *buf, int64_t val) -> uint32_t
{
    if (val < 0)
    {
        buf[0] = '-';
        return U64ToBuffer(buf + 1, (uint64_t)-val) + 1;
    }
    return U64ToBuffer(buf, (uint64_t)val);
}

void WriteFmt(auto &&consumer, const char *fmt, uint64_t fmtSize, ...)
{
    va_list args;
    va_start(args, fmtSize);

    ByteParser parser;
    parser.Init(fmt, fmtSize);

    while (parser.Left() > 0)
    {
        char ch = parser.Pop();

        switch (ch)
        {
        case '%': {
            char ch1 = parser.Left() ? parser.Pop() : '\0';

            switch (ch1)
            {

            case '.': {
                char ch2 = parser.Left() ? parser.Pop() : '\0';
                char ch3 = parser.Left() ? parser.Pop() : '\0';

                switch ((uint32_t)(ch2 | (ch3 << 8)))
                {
                case DWord("*s\0\0"): {
                    uint32_t len = va_arg(args, uint32_t);
                    const char *str = va_arg(args, const char *);
                    consumer((uint32_t)len, str);
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
                const char *s = va_arg(args, const char *);
                consumer(static_cast<uint32_t>(CStrLen(s)), s);
                break;
            }
            case 'd': {
                char buf[32];
                uint32_t len = S64ToBuffer(buf, va_arg(args, int));
                consumer(len, buf);
                break;
            }
            case 'u': {
                char buf[32];
                uint32_t len = U64ToBuffer(buf, va_arg(args, uint32_t));
                consumer(len, buf);
                break;
            }
            case 'p': {
                consumer(11, "0x[pointer]");
                break;
            }
            case '%': {
                consumer(1, "%");
                break;
            }

            default:
                NYLA_ASSERT(false);
                break;
            }

            break;
        }

        default: {
            const char *start = &parser.Peek() - 1;
            while (parser.Left() && parser.Peek() != '%')
                parser.Advance();

            consumer((uint32_t)(&parser.Peek() - start), start);
            break;
        }
        }
    }

    consumer(1, "\n");

    va_end(args);
}

void BufferWriteFmt(auto &&consumer, RegionAlloc &alloc, uint64_t bufferSize, const char *fmt, uint64_t fmtSize, ...)
{
    Span<char> buffer = alloc.PushArr<char>(bufferSize);
    uint64_t bufferUsed = 0;

    va_list args;
    va_start(args, fmtSize);

    WriteFmt(
        [&](uint32_t size, const char *data) -> void {
            if (bufferUsed + size > bufferSize)
            {
                consumer(buffer.Data(), bufferUsed);
                consumer(data, size);
                bufferUsed = 0;
            }
            else
            {
                MemCpy(buffer.Data() + bufferUsed, data, size);

                if (bufferUsed + size == bufferSize)
                {
                    consumer(buffer.Data(), bufferUsed);
                    bufferUsed = 0;
                }
            }
        },
        fmt, fmtSize, args);

    va_end(args);
}

} // namespace

void NYLA_API StringWriteFmt(char *out, uint64_t outSize, const char *fmt, uint64_t fmtSize, ...)
{
    va_list args;
    va_start(args, fmtSize);

    uint64_t outWritten = 0;

    WriteFmt(
        [&](const char *data, uint32_t size) -> void {
            NYLA_ASSERT(outWritten + size < outSize);
            MemCpy(out + outSize, data, size);
            outSize += size;
        },
        fmt, fmtSize, args);

    va_end(args);
}

void NYLA_API FileWriteFmt(FileHandle handle, RegionAlloc &alloc, const char *fmt, uint64_t fmtSize, ...)
{
    va_list args;
    va_start(args, fmtSize);

    BufferWriteFmt([handle](const char *data, uint32_t size) -> void { Platform::FileWrite(handle, size, data); },
                   alloc, 1024, fmt, fmtSize, args);

    va_end(args);
}

} // namespace nyla