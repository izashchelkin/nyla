#include "nyla/commons/pipeline_cache.h"

#include <cstdint>

#include "nyla/commons/array.h" // IWYU pragma: keep
#include "nyla/commons/asset_manager.h"
#include "nyla/commons/byteparser.h"
#include "nyla/commons/dev_log.h"
#include "nyla/commons/fmt.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/inline_vec.h"
#include "nyla/commons/inline_vec_def.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/mempage_pool.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/shader.h"
#include "nyla/commons/span.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/stringparser.h"
#include "nyla/commons/tokenparser.h"

namespace nyla
{

namespace
{

struct pipeline_cache_entry
{
    uint64_t vsGuid;
    uint64_t psGuid;
    uint64_t stateGuid;

    byteview debugName;
    inline_vec<rhi_vertex_binding_desc, 4> vertexBindings;
    inline_vec<rhi_vertex_attribute_desc, 8> vertexAttributes;
    inline_vec<rhi_texture_format, 4> colorTargetFormats;
    rhi_texture_format depthFormat;
    bool depthWriteEnabled;
    bool depthTestEnabled;
    rhi_cull_mode cullMode;
    rhi_front_face frontFace;

    rhi_graphics_pipeline currentPipeline;
};

struct pipeline_cache_state
{
    region_alloc storage;
    region_alloc scratch;
    handle_pool<pipeline_cache_handle, pipeline_cache_entry, 32> entries;
};
pipeline_cache_state *manager;

auto BuildPipeline(pipeline_cache_entry &entry) -> rhi_graphics_pipeline
{
    rhi_shader vs = GetShader(entry.vsGuid, rhi_shader_stage::Vertex);
    rhi_shader ps = GetShader(entry.psGuid, rhi_shader_stage::Pixel);

    if (entry.stateGuid)
    {
        byteview stateBytes = AssetManager::Get(entry.stateGuid);
        if (stateBytes.size)
        {
            byte_parser p;
            ByteParser::Init(p, stateBytes.data, stateBytes.size);

            while (ByteParser::HasNext(p))
            {
                StringParser::SkipWhitespace(p);
                if (!ByteParser::HasNext(p))
                    break;
                if (TokenParser::SkipLineComment(p))
                    continue;

                byteview key = TokenParser::ParseIdentifier(p);
                TokenParser::SkipLineWhitespace(p);
                byteview val = TokenParser::ParseIdentifier(p);

                // TODO: this is a mess. i would like to have a generic format for key value configs. on second thought
                // it's not that bad because it doesn't do stupid things like copying strings that into dynamic data
                // structures. so probably keep?

                if (Span::Eq(key, "depth_test"_s))
                    entry.depthTestEnabled = Span::Eq(val, "true"_s);
                else if (Span::Eq(key, "depth_write"_s))
                    entry.depthWriteEnabled = Span::Eq(val, "true"_s);
                else if (Span::Eq(key, "cull"_s))
                {
                    if (Span::Eq(val, "None"_s))
                        entry.cullMode = rhi_cull_mode::None;
                    else if (Span::Eq(val, "Back"_s))
                        entry.cullMode = rhi_cull_mode::Back;
                    else if (Span::Eq(val, "Front"_s))
                        entry.cullMode = rhi_cull_mode::Front;
                }
                else if (Span::Eq(key, "front_face"_s))
                {
                    if (Span::Eq(val, "CCW"_s))
                        entry.frontFace = rhi_front_face::CCW;
                    else if (Span::Eq(val, "CW"_s))
                        entry.frontFace = rhi_front_face::CW;
                }

                ByteParser::NextLine(p);
            }
        }
    }

    rhi_graphics_pipeline_desc desc{
        .debugName = entry.debugName,
        .vs = vs,
        .ps = ps,
        .vertexBindings =
            span<rhi_vertex_binding_desc>{InlineVec::DataPtr(entry.vertexBindings), entry.vertexBindings.size},
        .vertexAttributes =
            span<rhi_vertex_attribute_desc>{InlineVec::DataPtr(entry.vertexAttributes), entry.vertexAttributes.size},
        .colorTargetFormats =
            span<rhi_texture_format>{InlineVec::DataPtr(entry.colorTargetFormats), entry.colorTargetFormats.size},
        .depthFormat = entry.depthFormat,
        .depthWriteEnabled = entry.depthWriteEnabled,
        .depthTestEnabled = entry.depthTestEnabled,
        .cullMode = entry.cullMode,
        .frontFace = entry.frontFace,
    };

    RegionAlloc::Reset(manager->scratch);
    return Rhi::CreateGraphicsPipeline(manager->scratch, desc);
}

// TODO; how do these things handle dependencies? this one definitely depends on the shader one
// TODO: callbacks in general need to have RegionAlloc passed in!
void OnAssetChanged(uint64_t guid, byteview, void *)
{
    for (uint32_t i = 0; i < HandlePool::Capacity(manager->entries); ++i)
    {
        handle_slot<pipeline_cache_entry> &slot = manager->entries[i];
        if (!slot.used)
            continue;

        pipeline_cache_entry &entry = slot.data;
        if (entry.vsGuid != guid && entry.psGuid != guid && entry.stateGuid != guid)
            continue;

        rhi_graphics_pipeline rebuilt = BuildPipeline(entry);
        if (!rebuilt)
        {
            LOG("pipeline_cache: rebuild failed, keeping previous pipeline (" SV_FMT ")", SV_ARG(entry.debugName));
            uint8_t lineBuf[256]; // see todo before that about region alloc
            uint64_t n = StringWriteFmt(span<uint8_t>{lineBuf, sizeof(lineBuf)}, "pipeline fail: " SV_FMT ""_s,
                                        SV_ARG(entry.debugName));
            DevLog::Push(byteview{lineBuf, n});
            continue;
        }

        rhi_graphics_pipeline old = entry.currentPipeline;
        entry.currentPipeline = rebuilt;
        Rhi::DestroyGraphicsPipeline(old);
    }
}

} // namespace

namespace PipelineCache
{

void API Bootstrap()
{
    manager = &RegionAlloc::Alloc<pipeline_cache_state>(RegionAlloc::g_BootstrapAlloc);
    manager->storage = RegionAlloc::Create(MemPagePool::kChunkSize, 0);
    manager->scratch = RegionAlloc::Create(MemPagePool::kChunkSize, 0);

    AssetManager::Subscribe(OnAssetChanged, nullptr);
}

auto API Acquire(uint64_t vsGuid, uint64_t psGuid, const rhi_graphics_pipeline_desc &desc, uint64_t stateGuid)
    -> pipeline_cache_handle
{
    pipeline_cache_entry entry{
        .vsGuid = vsGuid,
        .psGuid = psGuid,
        .stateGuid = stateGuid,
        .debugName = RegionAlloc::CopyByteView(manager->storage, desc.debugName),
        .depthFormat = desc.depthFormat,
        .depthWriteEnabled = desc.depthWriteEnabled,
        .depthTestEnabled = desc.depthTestEnabled,
        .cullMode = desc.cullMode,
        .frontFace = desc.frontFace,
    };

    for (const rhi_vertex_binding_desc &b : desc.vertexBindings)
        InlineVec::Append(entry.vertexBindings, b);

    for (const rhi_vertex_attribute_desc &a : desc.vertexAttributes)
    {
        rhi_vertex_attribute_desc copy = a;
        copy.semantic = RegionAlloc::CopyByteView(manager->storage, a.semantic);
        InlineVec::Append(entry.vertexAttributes, copy);
    }

    for (const rhi_texture_format &f : desc.colorTargetFormats)
        InlineVec::Append(entry.colorTargetFormats, f);

    entry.currentPipeline = BuildPipeline(entry);

    return HandlePool::Acquire(manager->entries, entry);
}

auto API Resolve(pipeline_cache_handle h) -> rhi_graphics_pipeline
{
    return HandlePool::ResolveData(manager->entries, h).currentPipeline;
}

} // namespace PipelineCache

} // namespace nyla