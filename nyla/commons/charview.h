#pragma once

#include <span>
#include <vector>

namespace nyla {

template <typename T>
inline std::span<const char> CharViewVector(const std::vector<T>& in) {
  return {
      reinterpret_cast<const char*>(in.data()),
      in.size() * sizeof(T),
  };
}

template <typename T>
inline std::span<const char> CharViewRef(const T& in) {
  return {
      reinterpret_cast<const char*>(&in),
      sizeof(in),
  };
}

}  // namespace nyla