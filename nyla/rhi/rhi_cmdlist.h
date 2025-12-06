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

auto RhiCreateCmdList(RhiQueueType queueType) -> RhiCmdList;
void RhiNameCmdList(RhiCmdList, std::string_view name);
void RhiDestroyCmdList(RhiCmdList cmd);

auto RhiCmdSetCheckpoint(RhiCmdList cmd, uint64_t data) -> uint64_t;
auto RhiGetLastCheckpointData(RhiQueueType queueType) -> uint64_t;

auto RhiFrameBegin() -> RhiCmdList;
void RhiFrameEnd();

auto RhiFrameGetCmdList() -> RhiCmdList; // TODO: get rid of this

} // namespace nyla