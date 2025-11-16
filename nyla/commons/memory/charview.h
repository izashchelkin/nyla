#pragma once

#include <span>

namespace nyla {

using CharView = std::span<const char>;

template <typename T>
inline std::span<const char> CharViewSpan(std::span<T> in) {
  return {
      reinterpret_cast<const char*>(in.data()),
      in.size() * sizeof(T),
  };
}

template <typename T>
inline std::span<const char> CharViewPtr(T* in) {
  return {
      reinterpret_cast<const char*>(in),
      sizeof(*in),
  };
}

}  // namespace nyla