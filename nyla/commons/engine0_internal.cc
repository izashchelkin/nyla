#include "nyla/commons/engine0_internal.h"

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

        Str path = StringWriteFmt(alloc.template PushString<1024>(), AsStr(R"(D:\nyla\nyla\shaders\build\%s.hlsl.spv)"), name);
#endif
        // TODO: directory watch
        // PlatformFsWatchFile(path);

        Platform::FileOpen(path.CStr(), FileOpenMode::Read);

        std::vector<std::byte> code = Platform::ReadFile(path);
        const auto spirv = Span{reinterpret_cast<uint32_t *>(code.Data()), code.Size() / 4};

        RhiShader shader = Rhi::CreateShader(RhiShaderDesc{
            .stage = stage,
            .code = spirv,
        });
        return shader;
    });
}

} // namespace nyla::engine0_internal