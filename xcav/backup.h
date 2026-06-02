#pragma once

#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/span.h"

struct region_alloc;

namespace nyla
{

// ─── Backup ─────────────────────────────────────────────────────────────────

auto SaveBackup(byteview filePath, region_alloc &alloc) -> bool;
auto RestoreBackup(byteview filePath, region_alloc &alloc) -> bool;

} // namespace nyla
