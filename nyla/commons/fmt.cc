#include <cstdarg>

#include "nyla/commons/assert.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/platform.h"

namespace nyla
{

void LogWrapper(const char *fmt, ...)
{
    auto err = Platform::GetStderr();
    va_list args;
    va_start(args, fmt);
    FmtWrite(err, fmt, args);
    va_end(args);
    Platform::FileWrite(err, 1, "\n");
}

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

void FmtWrite(FileHandle handle, const char *fmt, va_list args)
{
    const char *p = fmt;
    while (*p)
    {
        if (*p == '%' && *(p + 1))
        {
            p++; // Skip '%'

            // Handle %.*s (The StrView / NYLA_SV_FMT logic)
            if (*p == '.' && *(p + 1) == '*' && *(p + 2) == 's')
            {
                int len = va_arg(args, int);
                const char *str = va_arg(args, const char *);
                Platform::FileWrite(handle, (uint32_t)len, str);
                p += 3;
                continue;
            }

            // Handle basic types
            switch (*p)
            {
            case 's': { // %s - Null-terminated string
                const char *s = va_arg(args, const char *);
                Platform::FileWrite(handle, (uint32_t)CStrLen(s), s);
                break;
            }
            case 'd': { // %d - Signed integer
                char buf[32];
                uint32_t len = S64ToBuffer(buf, va_arg(args, int));
                Platform::FileWrite(handle, len, buf);
                break;
            }
            case 'u': { // %u - Unsigned integer
                char buf[32];
                uint32_t len = U64ToBuffer(buf, va_arg(args, uint32_t));
                Platform::FileWrite(handle, len, buf);
                break;
            }
            case 'p': { // %p - Pointer (Hex)
                // Note: You'd need a HexToBuffer helper for this
                Platform::FileWrite(handle, 11, "0x[pointer]");
                break;
            }
            case '%': { // %% - Literal percent
                Platform::FileWrite(handle, 1, "%");
                break;
            }
            default: {
                NYLA_ASSERT(false);
            }
            }
        }
        else
        {
            const char *start = p;
            while (*p && *p != '%')
                p++;
            Platform::FileWrite(handle, (uint32_t)(p - start), start);
            if (!*p)
                break;
        }
        p++;
    }
}
} // namespace nyla