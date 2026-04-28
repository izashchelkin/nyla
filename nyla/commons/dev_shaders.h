#pragma once

#include "nyla/commons/macros.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

struct dev_shader_root
{
    byteview srcDir;
    byteview outDir;
};

namespace DevShaders
{

void API Bootstrap(span<const dev_shader_root> roots);

} // namespace DevShaders

} // namespace nyla
