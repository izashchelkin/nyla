#pragma once

#include "nyla/apps/3d_ball_maze/3d_ball_maze.h"

namespace nyla
{

class Scene
{
  public:
    virtual void Process(Game &game, RhiCmdList cmd, float dt, RhiRenderTargetView rtv, RhiDepthStencilView dsv) = 0;

  private:
};

} // namespace nyla
