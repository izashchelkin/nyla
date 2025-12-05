#pragma once

#include <type_traits>

namespace nyla
{

template <typename E> struct EnableBitMaskOps : std::false_type
{
};

template <typename E> constexpr bool EnableBitMaskOps_v = EnableBitMaskOps<E>::value;

template <typename E> constexpr auto operator|(E lhs, E rhs) -> std::enable_if_t<EnableBitMaskOps_v<E>, E>
{
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(lhs) | static_cast<U>(rhs));
}

template <typename E> constexpr auto operator|=(E &lhs, E rhs) -> std::enable_if_t<EnableBitMaskOps_v<E>, E &>
{
    using U = std::underlying_type_t<E>;
    lhs = static_cast<E>(static_cast<U>(lhs) | static_cast<U>(rhs));
    return lhs;
}

template <typename E> constexpr auto operator&(E lhs, E rhs) -> std::enable_if_t<EnableBitMaskOps_v<E>, E>
{
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(lhs) & static_cast<U>(rhs));
}

template <typename E> constexpr auto operator&=(E &lhs, E rhs) -> std::enable_if_t<EnableBitMaskOps_v<E>, E &>
{
    using U = std::underlying_type_t<E>;
    lhs = static_cast<E>(static_cast<U>(lhs) & static_cast<U>(rhs));
    return lhs;
}

template <typename E> constexpr auto operator^(E lhs, E rhs) -> std::enable_if_t<EnableBitMaskOps_v<E>, E>
{
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(lhs) ^ static_cast<U>(rhs));
}

template <typename E> constexpr auto operator^=(E &lhs, E rhs) -> std::enable_if_t<EnableBitMaskOps_v<E>, E &>
{
    using U = std::underlying_type_t<E>;
    lhs = static_cast<E>(static_cast<U>(lhs) ^ static_cast<U>(rhs));
    return lhs;
}

template <typename E> constexpr auto operator~(E val) -> std::enable_if_t<EnableBitMaskOps_v<E>, E>
{
    using U = std::underlying_type_t<E>;
    return static_cast<E>(~static_cast<U>(val));
}

template <typename E> constexpr auto Any(E val) -> std::enable_if_t<EnableBitMaskOps_v<E>, bool>
{
    using U = std::underlying_type_t<E>;
    return static_cast<U>(val) != 0;
}

} // namespace nyla