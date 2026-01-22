#include "nyla/commons/assert.h"
#include "nyla/commons/log.h"
#include "nyla/commons/memory/region_alloc.h"
#include "nyla/formats/gltf/gltf_parser.h"
#include "nyla/platform/platform.h"

namespace nyla
{

auto PlatformMain() -> int
{
    g_Platform->Init({});

    RegionAlloc regionAlloc{g_Platform->ReserveMemPages(1_GiB), 0, 1_GiB, RegionAllocCommitPageGrowth::GetInstance()};

#ifndef WIN32
    std::vector<std::byte> fileContent = g_Platform->ReadFile("/home/izashchelkin/Documents/test.glb");
#else
    std::vector<std::byte> fileContent = g_Platform->ReadFile("C:\\Users\\ihorz\\Desktop\\test.glb");
#endif

    RegionAlloc parserAlloc = regionAlloc.PushSubAlloc(256_MiB);
    GltfParser parser{parserAlloc, fileContent.data(), static_cast<uint32_t>(fileContent.size())};

    bool ok = parser.Parse();
    NYLA_ASSERT(ok);

    for (auto mesh : parser.GetMeshes())
    {
        NYLA_LOG("Mesh: " NYLA_SV_FMT, NYLA_SV_ARG(mesh.name));

        for (auto primitive : mesh.primitives)
        {
            NYLA_LOG(" AttributesCount: %lu", primitive.attributes.size());
            for (auto attribute : primitive.attributes)
            {
                auto &accessor = parser.GetAccessor(attribute.accessor);
                auto &bufferView = parser.GetBufferView(accessor.bufferView);

                NYLA_LOG("  Attribute: " NYLA_SV_FMT ": %d    BufferViewLength: %d", NYLA_SV_ARG(attribute.name),
                         attribute.accessor, bufferView.byteLength);
            }
        }
    }

    return 0;
}

} // namespace nyla