#pragma once

#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
#include "nyla/platform/platform.h"
#include <cstdint>
#include <span>

namespace nyla
{

class RegionAlloc;
class RegionAllocGrowthHandler
{
  public:
    [[nodiscard]]
    virtual auto TryGrow(RegionAlloc &alloc, uint32_t neededSize) -> bool = 0;
};

class RegionAllocBumpOnlyGrowth : public RegionAllocGrowthHandler
{
  public:
    static auto GetInstance() -> RegionAllocGrowthHandler &;

    auto TryGrow(RegionAlloc &alloc, uint32_t neededSize) -> bool final;
};

class RegionAllocCommitPageGrowth : public RegionAllocGrowthHandler
{
  public:
    static auto GetInstance() -> RegionAllocGrowthHandler &;

    auto TryGrow(RegionAlloc &alloc, uint32_t neededSize) -> bool final;
};

class RegionAlloc
{
    friend class RegionAllocCommitPageGrowth;
    friend class RegionAllocBumpOnlyGrowth;

  public:
    RegionAlloc(void *base, uint32_t size, uint32_t maxSize, RegionAllocGrowthHandler &growthHandler)
        : m_Base{(char *)base}, m_Size{size}, m_MaxSize{maxSize}, m_GrowthHandler{growthHandler}
    {
    }

    auto PushBytes(uint32_t size, uint32_t align) -> char *
    {
        AlignUp(m_Used, align);
        char *const ret = m_Base + m_Used;
        m_Used += size;

        if (m_Used > m_Size)
            NYLA_ASSERT(m_GrowthHandler.TryGrow(*this, m_Used));

        NYLA_ASSERT(m_Size <= m_MaxSize);
        return ret;
    }

    void Pop(void *p)
    {
        m_Used = (char *)p - m_Base;
        NYLA_ASSERT(m_Used <= m_Size);
    }

    void Reset()
    {
        m_Used = 0;
    }

    auto GetBase() -> char *
    {
        return m_Base;
    }

    auto GetBytesUsed() -> uint32_t
    {
        return m_Used;
    }

    template <typename T> auto Push() -> T *
    {
        static_assert(std::is_trivially_destructible_v<T>);

        T *const p = reinterpret_cast<T *>(PushBytes(sizeof(T), alignof(T)));
        return p;
    }

    template <typename T> auto PushArr(uint32_t n) -> std::span<T>
    {
        static_assert(std::is_trivially_destructible_v<T>);
        static_assert(AlignedUp(sizeof(T), alignof(T)) == sizeof(T));

        T *const p = reinterpret_cast<T *>(PushBytes(sizeof(T) * n, alignof(T)));
        return std::span{p, n};
    }

    auto PushSubAlloc(uint32_t size) -> RegionAlloc
    {
        // TODO: improve alignment handling?
        return RegionAlloc{PushBytes(size, 8), size, size, RegionAllocBumpOnlyGrowth::GetInstance()};
    }

  private:
    char *m_Base;
    RegionAllocGrowthHandler &m_GrowthHandler;
    uint32_t m_Size;
    uint32_t m_MaxSize;
    uint32_t m_Used{};
};

auto RegionAllocBumpOnlyGrowth::GetInstance() -> RegionAllocGrowthHandler &
{
    static RegionAllocBumpOnlyGrowth handler{};
    return handler;
}

[[nodiscard]]
auto RegionAllocBumpOnlyGrowth::TryGrow(RegionAlloc &alloc, uint32_t neededSize) -> bool
{
    alloc.m_Size = neededSize;

    return true;
}

auto RegionAllocCommitPageGrowth::GetInstance() -> RegionAllocGrowthHandler &
{
    static RegionAllocCommitPageGrowth handler{};
    return handler;
}

[[nodiscard]]
auto RegionAllocCommitPageGrowth::TryGrow(RegionAlloc &alloc, uint32_t neededSize) -> bool
{
    AlignUp(neededSize, g_Platform->GetMemPageSize());
    NYLA_ASSERT(neededSize <= alloc.m_MaxSize);

    char *const p = alloc.m_Base + alloc.m_Size;
    g_Platform->CommitMemPages(p, neededSize - alloc.m_Size);

    alloc.m_Size = neededSize;
    return true;
}

} // namespace nyla