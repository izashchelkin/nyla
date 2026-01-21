#include "nyla/commons/assert.h"
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

    GltfParser parser{regionAlloc, fileContent.data(), static_cast<uint32_t>(fileContent.size())};

    bool ok = parser.Parse();
    NYLA_ASSERT(ok);

    return 0;
}

} // namespace nyla