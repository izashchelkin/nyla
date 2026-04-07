#pragma once

#include <type_traits>

namespace nyla
{

template <typename T>
concept Plain =
    std::is_trivially_constructible_v<T> && std::is_trivially_destructible_v<T> && std::is_trivially_copyable_v<T>;

}