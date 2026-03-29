#pragma once

#include <cstdint>

#include "nyla/commons/align.h"
#include "nyla/commons/assert.h"
#include "nyla/commons/path.h"
#include "nyla/commons/platform.h"
#include "nyla/commons/str.h"

namespace nyla
{

class RegionAlloc
{
  public:
    void Init(void *base, uint64_t maxSize, bool ownsPages);

    auto PushBytes(uint64_t size, uint32_t align) -> char *;

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

    template <typename T> auto PushCopySpan(Span<const T> data) -> Span<T>
    {
        Span<T> p = PushArr<T>(data.size());
        MemCpy(p, data.data(), data.size_bytes());
        return p;
    }

    auto PushCopyStr(Str data) -> Span<char>
    {
        Span<char> p = PushArr<char>(data.Size());
        MemCpy(p.Data(), data.Data(), data.Size());
        return p;
    }

    template <typename T> auto PushArr(uint32_t n) -> Span<T>
    {
        static_assert(std::is_trivially_destructible_v<T>);
        static_assert(AlignedUp(sizeof(T), alignof(T)) == sizeof(T));

        T *const p = reinterpret_cast<T *>(PushBytes(sizeof(T) * n, alignof(T)));
        return Span{p, n};
    }

    auto PushSubAlloc(uint64_t size) -> RegionAlloc
    {
        AlignUp(size, Platform::GetMemPageSize());

        void *const p = PushBytes(size, Platform::GetMemPageSize());
        RegionAlloc subAlloc{};
        subAlloc.Init(p, size, false);
        return subAlloc;
    }

    auto PushPath() -> Path
    {
        Path path;
        path.Init(this);
        return path;
    }

    auto PushPath(Str path) -> Path
    {
        return PushPath().Append(path);
    }

    auto ClonePath(Path &path) -> Path
    {
        return PushPath().Append(path.GetStr());
    }

  private:
    bool m_OwnsPages;
    char *m_Base;
    uint64_t m_Size;
    uint64_t m_MaxSize;
    uint64_t m_Used;
};

} // namespace nyla