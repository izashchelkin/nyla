#include "nyla/commons/asset_import.h"

#include <cstdint>
#include <cstdlib>

#include "nyla/commons/asset_file_format.h"
#include "nyla/commons/cast.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/span.h"
#include "nyla/commons/span_def.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "nyla/commons/stb_image.h"

namespace nyla
{

auto ImportTextureFromPngOrJpg(byteview rawBytes, region_alloc &alloc) -> byteview
{
    int texWidth = 0;
    int texHeight = 0;
    uint8_t *pixelData =
        stbi_load_from_memory(rawBytes.data, CastI32(rawBytes.size), &texWidth, &texHeight, nullptr, 4);
    if (!pixelData)
        return byteview{};

    uint64_t pixelDataSize = (uint64_t)4 * texWidth * texHeight;
    uint64_t totalSize = sizeof(texture_blob_header) + pixelDataSize;

    span<uint8_t> dst = RegionAlloc::AllocArray<uint8_t>(alloc, totalSize);
    *(texture_blob_header *)dst.data = texture_blob_header{
        .width = (uint32_t)texWidth,
        .height = (uint32_t)texHeight,
        .format = 0,
        .pixelOffset = (uint32_t)sizeof(texture_blob_header),
    };
    MemCpy(dst.data + sizeof(texture_blob_header), pixelData, pixelDataSize);
    free(pixelData);

    return byteview{dst.data, totalSize};
}

} // namespace nyla
