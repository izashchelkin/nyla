#pragma once

#include "nyla/commons/handle.h"
#include <string_view>

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