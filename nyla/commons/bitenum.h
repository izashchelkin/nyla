#pragma once

#include <type_traits>

namespace nyla
{

template <typename E> struct EnableBitMaskOps : std::false_type
{
};

template <typename E> constexpr bool kEnableBitMaskOpsValue = EnableBitMaskOps<E>::value;

template <typename E>
concept BitmaskEnum = kEnableBitMaskOpsValue<E> && std::is_enum_v<E>;

template <BitmaskEnum E> constexpr auto operator|(E lhs, E rhs)
{
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(lhs) | static_cast<U>(rhs));
}

template <BitmaskEnum E> constexpr auto operator|=(E &lhs, E rhs)
{
    using U = std::underlying_type_t<E>;
    lhs = static_cast<E>(static_cast<U>(lhs) | static_cast<U>(rhs));
    return lhs;
}

template <BitmaskEnum E> constexpr auto operator&(E lhs, E rhs)
{
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(lhs) & static_cast<U>(rhs));
}

template <BitmaskEnum E> constexpr auto operator&=(E &lhs, E rhs)
{
    using U = std::underlying_type_t<E>;
    lhs = static_cast<E>(static_cast<U>(lhs) & static_cast<U>(rhs));
    return lhs;
}

template <BitmaskEnum E> constexpr auto operator^(E lhs, E rhs)
{
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(lhs) ^ static_cast<U>(rhs));
}

template <BitmaskEnum E> constexpr auto operator^=(E &lhs, E rhs)
{
    using U = std::underlying_type_t<E>;
    lhs = static_cast<E>(static_cast<U>(lhs) ^ static_cast<U>(rhs));
    return lhs;
}

template <BitmaskEnum E> constexpr auto operator~(E val)
{
    using U = std::underlying_type_t<E>;
    return static_cast<E>(~static_cast<U>(val));
}

template <BitmaskEnum E> constexpr auto Any(E val)
{
    using U = std::underlying_type_t<E>;
    return static_cast<U>(val) != 0;
}

} // namespace nyla