#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

namespace Clipboard
{

auto API Read(region_alloc &alloc) -> byteview;
void API Write(byteview text);

} // namespace Clipboard

} // namespace nyla
