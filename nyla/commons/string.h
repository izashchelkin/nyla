#pragma once

#include <string>
#include <string_view>

namespace nyla
{

inline void AsciiStrToUpperInPlace(std::string &s)
{
    for (char &c : s)
    {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 'a' && uc <= 'z')
            c = static_cast<char>(uc - ('a' - 'A'));
    }
}

inline std::string AsciiStrToUpper(std::string_view sv)
{
    std::string out(sv);
    AsciiStrToUpperInPlace(out);
    return out;
}

} // namespace nyla