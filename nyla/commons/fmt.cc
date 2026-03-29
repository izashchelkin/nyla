#include <cstdarg>
#include <cstdint>

#include "nyla/commons/byteparser.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/str.h"
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

} // namespace

void NYLA_API FmtWrite(FileHandle handle, Str fmt, va_list args)
{
    ByteParser parser;
    parser.Init(fmt.Data(), fmt.Size());

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
                    Platform::FileWrite(handle, (uint32_t)len, str);
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
                Platform::FileWrite(handle, (uint32_t)CStrLen(s), s);
                break;
            }
            case 'd': {
                char buf[32];
                uint32_t len = S64ToBuffer(buf, va_arg(args, int));
                Platform::FileWrite(handle, len, buf);
                break;
            }
            case 'u': {
                char buf[32];
                uint32_t len = U64ToBuffer(buf, va_arg(args, uint32_t));
                Platform::FileWrite(handle, len, buf);
                break;
            }
            case 'p': {
                Platform::FileWrite(handle, 11, "0x[pointer]");
                break;
            }
            case '%': {
                Platform::FileWrite(handle, 1, "%");
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

            Platform::FileWrite(handle, (uint32_t)(&parser.Peek() - start), start);
            break;
        }
        }
    }

    Platform::FileWrite(handle, 1, "\n");
}

} // namespace nyla