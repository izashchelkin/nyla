#pragma once

#include "nyla/commons/handle.h"
#include "nyla/commons/macros.h"

namespace nyla
{

struct texture : handle
{
};

namespace TextureManager
{

void API Bootstrap();

}

} // namespace nyla