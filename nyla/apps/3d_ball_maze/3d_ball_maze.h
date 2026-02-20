#include "nyla/engine/asset_manager.h"
#include "nyla/engine/render_targets.h"
#include "nyla/rhi/rhi_cmdlist.h"

namespace nyla
{

class Game
{
  public:
    void Init();
    void Process(RhiCmdList cmd, float dt);

  private:
    struct Assets
    {
        AssetManager::Mesh ball;
    };
    Assets m_Assets;

    RenderTargets m_RenderTargets;
};

} // namespace nyla