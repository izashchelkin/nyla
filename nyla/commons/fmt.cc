#include "nyla/commons/fmt.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "nyla/commons/array_def.h"
#include "nyla/commons/byteparser.h"
#include "nyla/commons/file.h"
#include "nyla/commons/hex.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/word.h"

namespace nyla
{

namespace
{

static array<uint8_t, 4096> buf; // TODO: return to this when multithreading

auto U64ToBuffer(uint8_t *buf, uint64_t val) -> uint32_t
{
    if (val == 0)
    {
        buf[0] = '0';
        return 1;
    }

    uint8_t temp[20];
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

void EmitPadded(auto &&consumer, const uint8_t *data, uint32_t len, uint32_t width, uint8_t padCh)
{
    if (width <= len)
    {
        consumer(data, len);
        return;
    }

    uint32_t padLen = width - len;
    uint8_t pad[64];
    ASSERT(padLen <= sizeof(pad));

    if (padCh == '0' && len > 0 && data[0] == '-')
    {
        consumer(data, 1);
        for (uint32_t i = 0; i < padLen; ++i)
            pad[i] = '0';
        consumer(pad, padLen);
        consumer(data + 1, len - 1);
    }
    else
    {
        for (uint32_t i = 0; i < padLen; ++i)
            pad[i] = padCh;
        consumer(pad, padLen);
        consumer(data, len);
    }
}

auto U64ToHexBuffer(uint8_t *buf, uint64_t val, bool upper = false) -> uint32_t
{
    if (val == 0)
    {
        buf[0] = '0';
        return 1;
    }

    uint8_t temp[16];
    uint32_t i = 0;
    while (val > 0)
    {
        if (upper)
            temp[i++] = GetHexCharUpper((uint8_t)(val & 0xF));
        else
            temp[i++] = GetHexChar((uint8_t)(val & 0xF));
        val >>= 4;
    }

    uint32_t len = i;
    for (uint32_t j = 0; j < len; ++j)
    {
        buf[j] = temp[len - 1 - j];
    }
    return len;
}

void WriteFmt(auto &&consumer, byteview fmt, va_list args)
{
    byte_parser parser;
    ByteParser::Init(parser, fmt.data, fmt.size);

    while (ByteParser::HasNext(parser))
    {
        uint8_t ch = ByteParser::Read(parser);

        switch (ch)
        {
        case '%': {
            uint8_t ch1 = ByteParser::ReadOrDefault(parser, '\0');

            uint32_t width = 0;
            uint8_t padCh = ' ';
            if (ch1 >= '0' && ch1 <= '9')
            {
                if (ch1 == '0')
                    padCh = '0';
                while (ch1 >= '0' && ch1 <= '9')
                {
                    width = width * 10 + (uint32_t)(ch1 - '0');
                    ch1 = ByteParser::ReadOrDefault(parser, '\0');
                }
            }

            switch (ch1)
            {

            case '.': {
                uint8_t ch2 = ByteParser::ReadOrDefault(parser, '\0');
                uint8_t ch3 = ByteParser::ReadOrDefault(parser, '\0');

                switch ((uint16_t)(ch2 | (ch3 << 8)))
                {
                case Word("*s"): {
                    uint64_t len = va_arg(args, uint64_t);
                    const uint8_t *str = va_arg(args, const uint8_t *);
                    consumer(str, len);
                    break;
                }

                default: {
                    ASSERT(false);
                    break;
                }
                }

                break;
            }

            case 's': {
                const uint8_t *s = va_arg(args, const uint8_t *);
                consumer(s, CStrLen(s, 1024));
                break;
            }

            case 'l': {
                uint8_t ch2 = ByteParser::ReadOrDefault(parser, '\0');

                switch (ch2)
                {
                case 'u': {
                    uint8_t buf[32];
                    uint64_t len = U64ToBuffer(buf, va_arg(args, unsigned long));
                    EmitPadded(consumer, buf, len, width, padCh);
                    break;
                }

                case 'd': {
                    uint8_t buf[32];
                    uint32_t len = S64ToBuffer(buf, va_arg(args, long));
                    EmitPadded(consumer, buf, len, width, padCh);
                    break;
                }

                case 'x': {
                    uint8_t buf[16];
                    uint64_t len = U64ToHexBuffer(buf, va_arg(args, unsigned long));
                    EmitPadded(consumer, buf, len, width, padCh);
                    break;
                }

                case 'X': {
                    uint8_t buf[16];
                    uint64_t len = U64ToHexBuffer(buf, va_arg(args, unsigned long), true);
                    EmitPadded(consumer, buf, len, width, padCh);
                    break;
                }

                case 'l': {
                    uint8_t ch3 = ByteParser::ReadOrDefault(parser, '\0');

                    switch (ch3)
                    {
                    case 'u': {
                        uint8_t buf[32];
                        uint64_t len = U64ToBuffer(buf, va_arg(args, unsigned long long));
                        EmitPadded(consumer, buf, len, width, padCh);
                        break;
                    }

                    case 'd': {
                        uint8_t buf[32];
                        uint32_t len = S64ToBuffer(buf, va_arg(args, long long));
                        EmitPadded(consumer, buf, len, width, padCh);
                        break;
                    }

                    case 'x': {
                        uint8_t buf[16];
                        uint64_t len = U64ToHexBuffer(buf, va_arg(args, unsigned long long));
                        EmitPadded(consumer, buf, len, width, padCh);
                        break;
                    }

                    case 'X': {
                        uint8_t buf[16];
                        uint64_t len = U64ToHexBuffer(buf, va_arg(args, unsigned long long), true);
                        EmitPadded(consumer, buf, len, width, padCh);
                        break;
                    }

                    default: {
                        TRAP();
                        UNREACHABLE();
                    }
                    }

                    break;
                }

                default: {
                    TRAP();
                    UNREACHABLE();
                }
                }

                break;
            }

            case 'd': {
                uint8_t buf[32];
                uint32_t len = S64ToBuffer(buf, va_arg(args, int));
                EmitPadded(consumer, buf, len, width, padCh);
                break;
            }

            case 'f': {
                double val = va_arg(args, double);
                char tmp[512];
                int n = ::snprintf(tmp, sizeof(tmp), "%f", val);
                if (n > 0)
                {
                    if ((uint64_t)n >= sizeof(tmp))
                        n = sizeof(tmp) - 1;
                    consumer((const uint8_t *)tmp, (uint64_t)n);
                }
                break;
            }

            case 'u': {
                uint8_t buf[32];
                uint32_t len = U64ToBuffer(buf, va_arg(args, uint32_t));
                EmitPadded(consumer, buf, len, width, padCh);
                break;
            }

            case 'x': {
                uint8_t buf[16];
                uint32_t len = U64ToHexBuffer(buf, va_arg(args, uint32_t));
                EmitPadded(consumer, buf, len, width, padCh);
                break;
            }

            case 'X': {
                uint8_t buf[16];
                uint32_t len = U64ToHexBuffer(buf, va_arg(args, uint32_t), true);
                EmitPadded(consumer, buf, len, width, padCh);
                break;
            }

            case 'c': {
                uint8_t c = (uint8_t)va_arg(args, int);
                consumer(&c, 1);
                break;
            }

            case 'b': {
                bool b = (bool)va_arg(args, int);
                if (b)
                    consumer((uint8_t *)"true", 4);
                else
                    consumer((uint8_t *)"false", 5);
                break;
            }

            case 'h': {
                uint8_t ch2 = ByteParser::ReadOrDefault(parser, '\0');

                switch (ch2)
                {
                case 'd': {
                    uint8_t buf[32];
                    uint32_t len = S64ToBuffer(buf, (int16_t)va_arg(args, int));
                    EmitPadded(consumer, buf, len, width, padCh);
                    break;
                }

                case 'u': {
                    uint8_t buf[32];
                    uint32_t len = U64ToBuffer(buf, (uint16_t)va_arg(args, unsigned int));
                    EmitPadded(consumer, buf, len, width, padCh);
                    break;
                }

                case 'x': {
                    uint8_t buf[16];
                    uint32_t len = U64ToHexBuffer(buf, (uint16_t)va_arg(args, unsigned int));
                    EmitPadded(consumer, buf, len, width, padCh);
                    break;
                }

                case 'X': {
                    uint8_t buf[16];
                    uint32_t len = U64ToHexBuffer(buf, (uint16_t)va_arg(args, unsigned int), true);
                    EmitPadded(consumer, buf, len, width, padCh);
                    break;
                }

                case 'h': {
                    uint8_t ch3 = ByteParser::ReadOrDefault(parser, '\0');

                    switch (ch3)
                    {
                    case 'd': {
                        uint8_t buf[32];
                        uint32_t len = S64ToBuffer(buf, (int8_t)va_arg(args, int));
                        EmitPadded(consumer, buf, len, width, padCh);
                        break;
                    }

                    case 'u': {
                        uint8_t buf[32];
                        uint32_t len = U64ToBuffer(buf, (uint8_t)va_arg(args, unsigned int));
                        EmitPadded(consumer, buf, len, width, padCh);
                        break;
                    }

                    case 'x': {
                        uint8_t buf[16];
                        uint32_t len = U64ToHexBuffer(buf, (uint8_t)va_arg(args, unsigned int));
                        EmitPadded(consumer, buf, len, width, padCh);
                        break;
                    }

                    case 'X': {
                        uint8_t buf[16];
                        uint32_t len = U64ToHexBuffer(buf, (uint8_t)va_arg(args, unsigned int), true);
                        EmitPadded(consumer, buf, len, width, padCh);
                        break;
                    }

                    default: {
                        TRAP();
                        UNREACHABLE();
                    }
                    }

                    break;
                }

                default: {
                    TRAP();
                    UNREACHABLE();
                }
                }

                break;
            }

            case 'z': {
                uint8_t ch2 = ByteParser::ReadOrDefault(parser, '\0');

                switch (ch2)
                {
                case 'u': {
                    uint8_t buf[32];
                    uint32_t len = U64ToBuffer(buf, va_arg(args, size_t));
                    EmitPadded(consumer, buf, len, width, padCh);
                    break;
                }

                case 'd': {
                    uint8_t buf[32];
                    uint32_t len = S64ToBuffer(buf, va_arg(args, ptrdiff_t));
                    EmitPadded(consumer, buf, len, width, padCh);
                    break;
                }

                case 'x': {
                    uint8_t buf[16];
                    uint32_t len = U64ToHexBuffer(buf, va_arg(args, size_t));
                    EmitPadded(consumer, buf, len, width, padCh);
                    break;
                }

                case 'X': {
                    uint8_t buf[16];
                    uint32_t len = U64ToHexBuffer(buf, va_arg(args, size_t), true);
                    EmitPadded(consumer, buf, len, width, padCh);
                    break;
                }

                default: {
                    TRAP();
                    UNREACHABLE();
                }
                }

                break;
            }

            case 'p': {
                void *ptr = va_arg(args, void *);
                uint8_t buf[18];
                buf[0] = '0';
                buf[1] = 'x';
                uint32_t len = U64ToHexBuffer(buf + 2, (uint64_t)(uintptr_t)ptr);
                consumer(buf, 2 + len);
                break;
            }

            case '%': {
                consumer((uint8_t *)"%", 1);
                break;
            }

            default:
                ASSERT(false);
                break;
            }

            break;
        }

        default: {
            const uint8_t *start = parser.at - 1;
            while (ByteParser::HasNext(parser) && ByteParser::Peek(parser) != '%')
                ByteParser::Advance(parser);

            consumer(start, parser.at - start);
            break;
        }
        }
    }
}

void BufferWriteFmt(auto &&consumer, span<uint8_t> buffer, byteview fmt, va_list args)
{
    uint64_t bufferUsed = 0;

    WriteFmt(
        [&](const uint8_t *data, uint32_t dataSize) -> void {
            if (dataSize == 0)
                return;

            if (dataSize >= buffer.size)
            {
                if (bufferUsed > 0)
                {
                    consumer(buffer.data, bufferUsed);
                    bufferUsed = 0;
                }

                consumer(data, dataSize);
                return;
            }

            if (bufferUsed + dataSize < buffer.size)
            {
                MemCpy(buffer.data + bufferUsed, data, dataSize);
                bufferUsed += dataSize;
                return;
            }

            uint64_t remainingInBuffer = buffer.size - bufferUsed;
            MemCpy(buffer.data + bufferUsed, data, remainingInBuffer);
            consumer(buffer.data, buffer.size);

            bufferUsed = dataSize - remainingInBuffer;
            if (bufferUsed > 0)
                MemCpy(buffer.data, data + remainingInBuffer, bufferUsed);
        },
        fmt, args);

    if (bufferUsed > 0)
        consumer(buffer.data, bufferUsed);
}

} // namespace

auto API StringWriteFmt(span<uint8_t> out, byteview fmt, va_list args) -> uint64_t
{
    uint64_t written = 0;

    WriteFmt(
        [&](const uint8_t *data, uint64_t dataSize) -> void {
            ASSERT(written + dataSize <= out.size);
            MemCpy(out.data + written, data, dataSize);
            written += dataSize;
        },
        fmt, args);

    return written;
}

void API FileWriteFmt(file_handle handle, span<uint8_t> buffer, byteview fmt, va_list args)
{
    BufferWriteFmt([handle](const uint8_t *data, uint64_t size) -> void { FileWrite(handle, (uint32_t)size, data); },
                   buffer, fmt, args);
}

void API FileWriteFmt(file_handle handle, byteview fmt, va_list args)
{
    FileWriteFmt(handle, buf, fmt, args);
}

void API FileWriteFmt(file_handle handle, span<uint8_t> buffer, byteview fmt, ...)
{
    va_list(args);
    va_start(args, fmt);
    FileWriteFmt(handle, buffer, fmt, args);
    va_end(args);
}

void API FileWriteFmt(file_handle handle, byteview fmt, ...)
{
    va_list(args);
    va_start(args, fmt);
    FileWriteFmt(handle, fmt, args);
    va_end(args);
}

auto API StringWriteFmt(span<uint8_t> out, byteview fmt, ...) -> uint64_t
{
    va_list(args);
    va_start(args, fmt);
    uint64_t written = StringWriteFmt(out, fmt, args);
    va_end(args);
    return written;
}

auto API StringWriteFmt(byteview fmt, va_list args) -> byteview
{
    return {buf.data, StringWriteFmt(buf, fmt, args)};
}

auto API StringWriteFmt(byteview fmt, ...) -> byteview
{
    va_list(args);
    va_start(args, fmt);
    byteview ret = {buf.data, StringWriteFmt(buf, fmt, args)};
    va_end(args);
    return ret;
}

} // namespace nyla