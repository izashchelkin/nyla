#include "nyla/commons/align.h"
#include <cstdint>
#include <span>

namespace nyla
{

class RegionAllocator
{
  public:
    RegionAllocator(uint32_t size);

    auto PushBytes(uint32_t size, uint32_t align) -> char *;
    void Pop(void *);
    void Reset();

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

    auto GetBytesUsed() -> uint32_t;

  private:
    uint32_t m_Size;
    uint32_t m_Used{};
    uint32_t m_Commited;
    char *m_Base;
};

} // namespace nyla