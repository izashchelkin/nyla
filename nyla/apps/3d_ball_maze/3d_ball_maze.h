#include "nyla/engine/asset_manager.h"
#include "nyla/rhi/rhi_cmdlist.h"
#include "nyla/rhi/rhi_texture.h"

namespace nyla
{

class Game
{
  public:
    void Init();
    void Process(RhiCmdList cmd, float dt);
    void Render(RhiCmdList cmd, RhiRenderTargetView rtv);

  private:
    struct Assets
    {
        AssetManager::Mesh ball;
    };
    Assets m_Assets;
};

} // namespace nyla