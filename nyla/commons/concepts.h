#pragma once

#include <type_traits>

namespace nyla
{

template <typename T>
concept is_plain = std::is_trivial_v<T> && std::is_trivially_copyable_v<T> && ((sizeof(T) % alignof(T)) == 0);

}