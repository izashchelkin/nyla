#include "nyla/platform/platform.h"

namespace nyla
{

Platform *g_Platform = new Platform(g_PlatformImpl);

}