#pragma once

#include <cstdint>
#include <type_traits>

#include "nyla/commons/align.h"
#include "nyla/commons/array.h"
#include "nyla/commons/inline_string.h"
#include "nyla/commons/inline_vec.h"

namespace nyla
{

struct NYLA_API RegionAlloc
{
    static constexpr inline uint64_t kMinAlign = 16;

    bool m_OwnsPages;
    char *m_Base;
    uint64_t m_Size;
    uint64_t m_MaxSize;
    uint64_t m_Used;

    void Init(void *base, uint64_t maxSize, bool ownsPages);

    auto PushBytes(uint64_t size, uint64_t align) -> char *;

    void Pop(void *p);

    void Reset()
    {
        m_Used = 0;
    }

    [[nodiscard]]
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

    [[nodiscard]]
    auto GetAt() -> char *
    {
        return m_Base + m_Used;
    }

    [[nodiscard]]
    auto GetAt() const -> const char *
    {
        return m_Base + m_Used;
    }

    [[nodiscard]]
    auto GetMaxSize() -> uint32_t
    {
        return m_MaxSize;
    }

    template <typename T> auto Push() -> T *
    {
        static_assert(std::is_trivially_destructible_v<T>);

        T *const p = reinterpret_cast<T *>(PushBytes(sizeof(T), alignof(T)));
        // *p = {};

        return p;
    }

    template <typename T> auto Push(const T &initializer) -> T *
    {
        T *const p = Push<T>();
        *p = initializer;
        return p;
    }

    template <typename T> auto PushCopySpan(Span<T> data) -> Span<T>
    {
        Span<T> p = PushArr<T>(data.Size());
        MemCpy(p.Data(), data.Data(), data.SizeBytes());
        return p;
    }

    auto PushCopyStr(Str data) -> Span<char>
    {
        Span<char> p = PushArr<char>(data.Size());
        MemCpy(p.Data(), data.Data(), data.Size());
        return p;
    }

    template <typename T> auto PushArr(uint64_t n) -> Span<T>
    {
        uint64_t s = sizeof(T) * n;

        static_assert(std::is_trivially_destructible_v<T>);
        static_assert(AlignedUp(sizeof(T), alignof(T)) == sizeof(T));

        T *const p = reinterpret_cast<T *>(PushBytes(sizeof(T) * n, alignof(T)));
        return Span{p, n};
    }

    template <typename T, uint64_t N> auto PushVec() -> InlineVec<T, N> &
    {
        return *Push<InlineVec<T, N>>();
    }

    template <uint64_t N> auto PushString() -> InlineString<N> &
    {
        return *Push<InlineString<N>>();
    }

    auto VoidScope(uint64_t size, auto &&fn) -> void
    {
        auto mark = GetAt();
        auto alloc = PushSubAlloc(size);
        fn(alloc);
        Pop(mark);
    }

    auto Scope(uint64_t size, auto &&fn)
    {
        auto mark = GetAt();
        auto alloc = PushSubAlloc(size);

        auto &&ret = fn(alloc);
        Pop(mark);
        return ret;
    }

    auto PushSubAlloc(uint64_t size) -> RegionAlloc;
};

} // namespace nyla