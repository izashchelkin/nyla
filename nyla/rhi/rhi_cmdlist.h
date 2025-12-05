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

auto RhiCreateCmdList(RhiQueueType queue_type) -> RhiCmdList;
void RhiNameCmdList(RhiCmdList, std::string_view name);
void RhiDestroyCmdList(RhiCmdList cmd);

auto __RhiCmdSetCheckpoint(RhiCmdList cmd, uint64_t data) -> uint64_t;
auto __RhiGetLastCheckpointData(RhiQueueType queue_type) -> uint64_t;

auto RhiFrameBegin() -> RhiCmdList;
void RhiFrameEnd();

auto RhiFrameGetCmdList() -> RhiCmdList; // TODO: get rid of this

} // namespace nyla