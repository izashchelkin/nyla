#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <variant>

namespace nyla
{

enum class SpirviewShaderStage
{
    Unknown,
    Vertex,
    Fragment,
    Compute,
};

enum class SpirviewResourceKind
{
    Unknown,
    ConstantBuffer,
    StorageBuffer,
    Texture,
    RWTexture,
    Sampler,
    PushConstant,
};

struct SpirviewType
{
    bool block;
};

struct SpirviewResource
{
    uint32_t typeId;
    SpirviewResourceKind kind;
    uint32_t set;
    uint32_t binding;
    uint32_t arrayCount;
};

using SpirviewRecord = std::variant<std::monostate, SpirviewType, SpirviewResource>;

struct SpirviewReflectResult
{
    SpirviewShaderStage stage;

    std::array<SpirviewRecord, 1024> records;
    uint32_t typesCount;
    std::array<uint32_t, 32> types;
    uint32_t resourcesCount;
    std::array<uint32_t, 32> resources;
};

auto SpirviewReflect(std::span<const uint32_t> spirv, SpirviewReflectResult *result) -> bool;

} // namespace nyla