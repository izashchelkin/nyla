#pragma once

#include <span>

namespace nyla {

template <typename T>
inline std::span<const char> CharViewSpan(std::span<T> in) {
  return {
      reinterpret_cast<const char*>(in.data()),
      in.size() * sizeof(T),
  };
}

template <typename T>
inline std::span<const char> CharView(T* in) {
  return {
      reinterpret_cast<const char*>(in),
      sizeof(*in),
  };
}

}  // namespace nyla