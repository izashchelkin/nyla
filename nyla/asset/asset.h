#pragma once

#include <cstdint>

namespace nyla
{

struct AssetDesc
{
    enum AssetType
    {
        Image,
        Mesh
    };

    enum ImageType
    {
        PNG,
        Grayscale,
    };

    enum MeshType
    {
        Internal,
        Gltf,
    };

    uint32_t uid;
    uint32_t type;
    uint64_t dataSize;
    uint64_t dataOffset;
    union {
        struct ImageMeta
        {
            ImageType imageType;
            uint32_t width;
            uint32_t height;
        } image;

        struct MeshMeta
        {
            MeshType meshType;
        } mesh;
    } meta;
};

struct AssetPackerHeader
{
    uint32_t count;
    // char comment[];
};

struct AssetDescTable
{
    uint32_t count;
    // AssetDesc assets[];
};

} // namespace nyla