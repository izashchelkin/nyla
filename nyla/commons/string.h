#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace nyla
{

inline auto AsciiChrToUpper(uint32_t ch) -> uint32_t
{
    if (ch >= 'a' && ch <= 'z')
        return ch - ('a' - 'A');
    else
        return ch;
}

inline void AsciiStrToUpperInPlace(std::string &s)
{
    for (char &c : s)
        c = static_cast<char>(AsciiChrToUpper(c));
}

inline auto AsciiStrToUpper(std::string_view sv) -> std::string
{
    std::string out(sv);
    AsciiStrToUpperInPlace(out);
    return out;
}

} // namespace nyla