#pragma once

#include "nyla/commons/bitenum.h"
#include <string>
#include <string_view>

namespace nyla
{

enum class PlatformDirWatchEventType
{
    Modified, Deleted, MovedFrom, MovedTo
};
NYLA_BITENUM(PlatformDirWatchEventType);

struct PlatformDirWatchEvent
{
    std::string_view name;
    PlatformDirWatchEventType mask;
};

struct PlatformDirWatchState;

class PlatformDirWatch
{
  public:
    PlatformDirWatch(std::string directoryPath) : m_DirectoryPath{std::move(directoryPath)}
    {
    }

    void Init();
    void Destroy();

    auto PollChange(PlatformDirWatchEvent &outChange) -> bool;

  private:
    std::string m_DirectoryPath;
    PlatformDirWatchState *m_State{};
};

} // namespace nyla