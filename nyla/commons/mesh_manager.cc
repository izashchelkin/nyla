#include "nyla/commons/mesh_manager.h"

#include <cstdint>

#include "nyla/commons/asset_file.h"
#include "nyla/commons/gltf.h"
#include "nyla/commons/gpu_upload.h"
#include "nyla/commons/handle_pool.h"
#include "nyla/commons/macros.h"
#include "nyla/commons/mem.h"
#include "nyla/commons/region_alloc.h"
#include "nyla/commons/region_alloc_def.h"
#include "nyla/commons/rhi.h"
#include "nyla/commons/span_def.h"
#include "nyla/commons/texture_manager.h"

namespace nyla
{

namespace
{

enum class mesh_state
{
    NotUploaded = 0,
    Uploaded
};

struct mesh_metadata
{
    uint64_t guidGltf;
    uint64_t guidBin;
    mesh_state state;
    uint64_t vertexBufferOffset;
    uint64_t indexBufferOffset;
    uint32_t indexCount;
    texture texture;
};

struct mesh_manager
{
    handle_pool<mesh, mesh_metadata, 128> meshes;
};
mesh_manager *manager;

} // namespace

namespace MeshManager
{

void API Bootstrap()
{
    manager = &RegionAlloc::Alloc<mesh_manager>(RegionAlloc::g_BootstrapAlloc);
}

void API Update(region_alloc &alloc, rhi_cmdlist cmd, byteview assetFile)
{
    void *allocMark = alloc.at;

    for (auto &slot : manager->meshes)
    {
        if (!slot.used)
            continue;

        mesh_metadata &metadata = slot.data;
        if (metadata.state != mesh_state::NotUploaded)
            continue;

        LOG("Uploading mesh '" SV_FMT "' '" SV_FMT "'", metadata.guidGltf, metadata.guidBin);
        RegionAlloc::Reset(alloc, allocMark);

        gltf_parser parser{};
        parser.jsonChunk = AssetFileGetData(assetFile, metadata.guidGltf);
        parser.binChunk = AssetFileGetData(assetFile, metadata.guidBin);

        ASSERT(GltfParser::Parse(parser, alloc));

        ASSERT(parser.images.size == 1);

        { // TODO:
            gltf_image image = Span::Front(parser.images);
            metadata.texture = TextureManager::DeclareTexture(assetFile, QWord("DUMMY??"));
        }

        for (const auto &mesh : parser.meshes)
        {
            const auto &meshPrimitive = Span::Front(mesh.primitives);

            {
                gltf_accessor indices = parser.accessors[meshPrimitive.indices];
                ASSERT(indices.type == gltf_accessor_type::SCALAR);
                ASSERT(indices.componentType == gltf_accessor_component_type::UNSIGNED_SHORT);

                gltf_buffer_view bufferView = parser.bufferViews[indices.bufferView];

                uint32_t bufferSize = indices.count * GetGltfAccessorSize(indices);
                char *const uploadMemory = GpuUpload::CmdCopyStaticIndices(cmd, bufferSize, metadata.indexBufferOffset);

                byteview indicesData = GltfParser::GetAccessorData(parser, indices);
                ASSERT((Span::SizeBytes(indicesData) % sizeof(uint16_t)) == 0);

                MemCpy(uploadMemory, indicesData.data, Span::SizeBytes(indicesData));

                metadata.indexCount = Span::SizeBytes(indicesData) / sizeof(uint16_t);
                ASSERT(metadata.indexCount % 3 == 0);
            }

            {
                auto &attrs = meshPrimitive.attributes;

                uint32_t stride = 0;

                auto alignStride = [&](gltf_accessor accessor) -> void {
                    const uint32_t componentSize = GetGltfAccessorComponentSize(accessor.componentType);
                    stride = AlignedUp(stride, componentSize);
                };
                auto countStride = [&](gltf_accessor accessor) -> uint32_t {
                    alignStride(accessor);
                    const uint32_t offset = stride;

                    stride += GetGltfAccessorSize(accessor);
                    return offset;
                };

                gltf_accessor pos;

                ASSERT(GltfParser::FindAttributeAccessor(parser, attrs, "POSITION"_s, pos));
                ASSERT(pos.type == gltf_accessor_type::VEC3);
                ASSERT(pos.componentType == gltf_accessor_component_type::FLOAT);
                const uint32_t posOffset = countStride(pos);

                gltf_accessor norm;
                ASSERT(GltfParser::FindAttributeAccessor(parser, attrs, "NORMAL"_s, norm));
                ASSERT(norm.type == gltf_accessor_type::VEC3);
                ASSERT(norm.componentType == gltf_accessor_component_type::FLOAT);
                ASSERT(norm.count == pos.count);
                const uint32_t normOffset = countStride(norm);

                gltf_accessor texCoord;
                ASSERT(GltfParser::FindAttributeAccessor(parser, attrs, "TEXCOORD_0"_s, texCoord));
                ASSERT(texCoord.type == gltf_accessor_type::VEC2);
                ASSERT(texCoord.componentType == gltf_accessor_component_type::FLOAT);
                ASSERT(texCoord.count == pos.count);
                const uint32_t texCoordOffset = countStride(texCoord);

                alignStride(pos);
                const uint32_t bufferSize = stride * pos.count;

                byteview posBufferView = GltfParser::GetAccessorData(parser, pos);
                byteview normBufferView = GltfParser::GetAccessorData(parser, norm);
                byteview texCoordBufferView = GltfParser::GetAccessorData(parser, texCoord);

                char *const uploadMemory =
                    GpuUpload::CmdCopyStaticVertices(cmd, bufferSize, metadata.vertexBufferOffset);

                for (uint32_t i = 0; i < pos.count; ++i)
                {
                    auto copyAttribute = [uploadMemory, i, stride](gltf_accessor accessor, uint32_t offset,
                                                                   const uint8_t *byteBufferViewData) -> void {
                        void *const dst = uploadMemory + static_cast<uint64_t>(i * stride + offset);
                        const void *const src =
                            byteBufferViewData + static_cast<uint64_t>(i * GetGltfAccessorSize(accessor));
                        memcpy(dst, src, GetGltfAccessorSize(accessor));
                    };

                    copyAttribute(pos, posOffset, posBufferView.data);
                    copyAttribute(norm, normOffset, normBufferView.data);
                    copyAttribute(texCoord, texCoordOffset, texCoordBufferView.data);
                }
            }
        }

        metadata.state = mesh_state::Uploaded;
    }
}

auto API DeclareTexture(byteview assetFileData, uint64_t guidGltf, uint64_t guidBin) -> mesh
{
    return HandlePool::Acquire(manager->meshes, mesh_metadata{
                                                    .guidGltf = guidGltf,
                                                    .guidBin = guidGltf,
                                                    .state = mesh_state::NotUploaded,
                                                });
}

void API CmdBindMesh(rhi_cmdlist cmd, mesh Mesh)
{
    const auto &meshData = HandlePool::ResolveData(manager->meshes, Mesh);
    ASSERT(meshData.state == mesh_state::Uploaded);

    GpuUpload::CmdBindStaticMeshVertexBuffer(cmd, meshData.vertexBufferOffset);
    GpuUpload::CmdBindStaticMeshIndexBuffer(cmd, meshData.indexBufferOffset);
}

void API CmdDrawMesh(rhi_cmdlist cmd, mesh Mesh)
{
    const auto &meshData = HandlePool::ResolveData(manager->meshes, Mesh);
    ASSERT(meshData.state == mesh_state::Uploaded);

    Rhi::CmdDrawIndexed(cmd, meshData.indexCount, 0, 1, 0, 0);
}

} // namespace MeshManager

} // namespace nyla
