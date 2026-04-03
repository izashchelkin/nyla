#pragma once

#include "nyla/commons/asset_manager.h"
#include "nyla/commons/render_targets.h"
#include "nyla/commons/rhi.h"

namespace nyla
{

class Game
{
  public:
    void Init();
    void Process(RhiCmdList cmd, float dt);

    [[nodiscard]]
    auto GetAssets() -> const auto &
    {
        return m_Assets;
    }

  private:
    struct Assets
    {
        AssetManager::Mesh ball;
        AssetManager::Mesh cube;
    };
    Assets m_Assets;

    RenderTargets m_RenderTargets;
};

} // namespace nyla