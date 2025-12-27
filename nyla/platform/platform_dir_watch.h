#pragma once

#include "nyla/commons/bitenum.h"
#include <string_view>

namespace nyla
{

enum class PlatformDirWatchEventType
{
    Modified,
    Deleted,
    MovedFrom,
    MovedTo
};
NYLA_BITENUM(PlatformDirWatchEventType);

struct PlatformDirWatchEvent
{
    std::string_view name;
    PlatformDirWatchEventType mask;
};

class PlatformDirWatch
{
  public:
    void Init(const char *path);
    void Destroy();

    auto Poll(PlatformDirWatchEvent &outChange) -> bool;

  private:
    class Impl;
    Impl *m_Impl{};
};

} // namespace nyla