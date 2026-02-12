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
    virtual auto TryGrow(RegionAlloc &alloc, uint64_t neededSize) -> bool = 0;
};

class RegionAllocBumpOnlyGrowth : public RegionAllocGrowthHandler
{
  public:
    static auto GetInstance() -> RegionAllocGrowthHandler &
    {
        static RegionAllocBumpOnlyGrowth handler{};
        return handler;
    }

    auto TryGrow(RegionAlloc &alloc, uint64_t neededSize) -> bool final;
};

class RegionAllocCommitPageGrowth : public RegionAllocGrowthHandler
{
  public:
    static auto GetInstance() -> RegionAllocGrowthHandler &
    {
        static RegionAllocCommitPageGrowth handler{};
        return handler;
    }

    auto TryGrow(RegionAlloc &alloc, uint64_t neededSize) -> bool final;
};

class Path;

class RegionAlloc
{
    friend class RegionAllocCommitPageGrowth;
    friend class RegionAllocBumpOnlyGrowth;

  public:
    void Init(void *base, uint64_t maxSize, RegionAllocGrowthHandler &growthHandler)
    {
        m_Used = 0;
        m_Size = 0;
        m_MaxSize = maxSize;
        m_GrowthHandler = &growthHandler;
        m_Base = (char *)base;

        if (!m_Base)
            m_Base = g_Platform.ReserveMemPages(maxSize);
    }

    auto PushBytes(uint64_t size, uint32_t align) -> char *
    {
        AlignUp<uint64_t>(m_Used, align);
        char *const ret = m_Base + m_Used;
        m_Used += size;

        if (m_Used > m_Size)
            NYLA_ASSERT(m_GrowthHandler->TryGrow(*this, m_Used));

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

    [[nodiscard]]
    auto GetBase() const -> const char *
    {
        return m_Base;
    }

    [[nodiscard]]
    auto GetBytesUsed() const -> uint32_t
    {
        return m_Used;
    }

    auto GetAt() -> char *
    {
        return m_Base + m_Used;
    }

    auto GetMaxSize() -> uint32_t
    {
        return m_MaxSize;
    }

    template <typename T> auto Push() -> T *
    {
        static_assert(std::is_trivially_destructible_v<T>);

        T *const p = reinterpret_cast<T *>(PushBytes(sizeof(T), alignof(T)));
        return p;
    }

    template <typename T> auto Push(const T &initializer) -> T *
    {
        T *const p = Push<T>();
        *p = initializer;
        return p;
    }

    template <typename T> auto PushCopySpan(std::span<const T> data) -> std::span<T>
    {
        std::span<T> p = PushArr<T>(data.size());
        memcpy(p, data.data(), data.size_bytes());
        return p;
    }

    auto PushCopyStrView(std::string_view data) -> std::span<char>
    {
        std::span<char> p = PushArr<char>(data.size());
        memcpy(p.data(), data.data(), data.size());
        return p;
    }

    template <typename T> auto PushArr(uint32_t n) -> std::span<T>
    {
        static_assert(std::is_trivially_destructible_v<T>);
        static_assert(AlignedUp(sizeof(T), alignof(T)) == sizeof(T));

        T *const p = reinterpret_cast<T *>(PushBytes(sizeof(T) * n, alignof(T)));
        return std::span{p, n};
    }

    auto PushSubAlloc(uint64_t size) -> RegionAlloc
    {
        AlignUp(size, g_Platform.GetMemPageSize());

        void *const p = PushBytes(size, g_Platform.GetMemPageSize());
        RegionAlloc subAlloc{};
        subAlloc.Init(p, size, RegionAllocCommitPageGrowth::GetInstance());
        return subAlloc;
    }

    auto PushPath() -> Path;
    auto PushPath(std::string_view) -> Path;

  private:
    char *m_Base;
    RegionAllocGrowthHandler *m_GrowthHandler;
    uint64_t m_Size;
    uint64_t m_MaxSize;
    uint64_t m_Used;
};

inline auto RegionAllocBumpOnlyGrowth::TryGrow(RegionAlloc &alloc, uint64_t neededSize) -> bool
{
    alloc.m_Size = neededSize;

    return true;
}

inline auto RegionAllocCommitPageGrowth::TryGrow(RegionAlloc &alloc, uint64_t neededSize) -> bool
{
    AlignUp(neededSize, g_Platform.GetMemPageSize());
    NYLA_ASSERT(neededSize <= alloc.m_MaxSize);

    char *const p = alloc.m_Base + alloc.m_Size;
    g_Platform.CommitMemPages(p, neededSize - alloc.m_Size);

    alloc.m_Size = neededSize;
    return true;
}

} // namespace nyla