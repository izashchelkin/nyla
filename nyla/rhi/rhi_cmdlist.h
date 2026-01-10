#pragma once

#include "nyla/commons/handle.h"

namespace nyla
{

enum class RhiQueueType
{
    Graphics,
    Transfer
};

struct RhiCmdList : Handle
{
};

} // namespace nyla