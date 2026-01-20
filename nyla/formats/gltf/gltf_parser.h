#pragma once

#include "nyla/commons/word.h"
#include <cstdint>

namespace nyla
{

class GltfParser
{
    static constexpr uint32_t kMagic = Word("glTF");

  public:
    GltfParser(void *data, uint32_t byteLength): m_At{data}, m_BytesLeft{byteLength}
    {
    }

    auto Parse() -> bool;

  private:
    void *m_At;
    uint32_t m_BytesLeft;
};

} // namespace nyla