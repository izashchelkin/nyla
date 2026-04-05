#include "nyla/commons/engine0_internal.h"

#include "nyla/commons/array.h"
#include "nyla/commons/engine.h"
#include "nyla/commons/platform_base.h"
#include "nyla/commons/rhi.h"

namespace nyla::engine0_internal
{

auto GetShader(const char *name, RhiShaderStage stage) -> RhiShader
{
    return Engine::GetPerFrameAlloc().Scope(1024, [&](auto &alloc) -> RhiShader {

#if defined(__linux__) // TODO : deal with this please
        const std::string path = std::format("/home/izashchelkin/nyla/nyla/shaders/build/{}.hlsl.spv", name);
#else

        auto path = StringWriteFmt(alloc.template PushString<1024>(), AsStr(R"(D:\nyla\nyla\shaders\build\%s.hlsl.spv)"), name);
#endif
        // TODO: directory watch
        // PlatformFsWatchFile(path);

#if 0
        const Span<char> code = Platform::FileRead(alloc, path, 1 << 20);
        const Span<uint32_t> spirv = code.Cast<uint32_t>();
#else
        const Span<uint32_t> spirv = Platform::FileRead(alloc, path, 1 << 20).template Cast<uint32_t>();
#endif

        RhiShader shader = Rhi::CreateShader(RhiShaderDesc{
            .stage = stage,
            .code = spirv,
        });
        return shader;
    });
}

} // namespace nyla::engine0_internal