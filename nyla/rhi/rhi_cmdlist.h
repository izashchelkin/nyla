#pragma once

#include "nyla/rhi/rhi_handle.h"

namespace nyla
{

enum class RhiQueueType
{
    Graphics,
    Transfer
};

struct RhiCmdList : RhiHandle
{
};

RhiCmdList RhiCreateCmdList(RhiQueueType queue_type);
void RhiNameCmdList(RhiCmdList, std::string_view name);
void RhiDestroyCmdList(RhiCmdList cmd);

uint64_t __RhiCmdSetCheckpoint(RhiCmdList cmd, uint64_t data);
uint64_t __RhiGetLastCheckpointData(RhiQueueType queue_type);

RhiCmdList RhiFrameBegin();
void RhiFrameEnd();

RhiCmdList RhiFrameGetCmdList(); // TODO: get rid of this

} // namespace nyla