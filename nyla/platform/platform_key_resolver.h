#pragma once

#include <cstdint>

namespace nyla
{

enum class KeyPhysical;

class PlatformKeyResolver
{
  public:
    void Init();
    void Destroy();

    auto ResolveKeyCode(KeyPhysical) -> uint32_t;

  private:
    class Impl;
    Impl *m_Impl{};
};


} // namespace nyla