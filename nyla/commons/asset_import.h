#pragma once

#include "nyla/commons/macros.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/span_def.h"

namespace nyla
{

// Decode a PNG/JPG byte stream into the in-memory texture blob format
// (texture_blob_header followed by RGBA8 pixel data). Output bytes are
// allocated in `alloc`. Returns an empty byteview on decode failure.
auto API ImportTextureFromPngOrJpg(byteview rawBytes, region_alloc &alloc) -> byteview;

} // namespace nyla
