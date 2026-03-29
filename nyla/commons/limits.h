#include <concepts>
#include <cstdint>

namespace nyla
{

template <typename T> struct Limits
{
};

template <std::unsigned_integral T> struct Limits<T>
{
    static constexpr auto Max() -> T
    {
        return (T)-1;
    }

    static constexpr auto Min() -> T
    {
        return (T)0;
    }
};

template <std::signed_integral T> struct Limits<T>
{
    static constexpr auto Max() -> T
    {
        return (T)(((uint64_t)1 << (sizeof(T) * 8 - 1)) - 1);
    }

    static constexpr auto Min() -> T
    {
        return (T)((uint64_t)1 << (sizeof(T) * 8 - 1));
    }
};

} // namespace nyla