#pragma once

#include <cstddef>
#include <cstdint>

#include "absl/log/check.h"

namespace nyla {

struct Handle {
  uint32_t gen;
  uint32_t index;
};

inline bool HandleIsSet(Handle handle) {
  return handle.gen;
}

template <typename Data, size_t Size>
struct HandlePool {
  struct {
    Data data;
    uint32_t gen;
    bool used;
  } slots[Size];
};

template <typename Data, size_t Size>
inline Handle HandleAcquire(HandlePool<Data, Size>& pool, Data data, bool allow_intern = false) {
  Handle ret_handle;

  for (uint32_t i = 0; i < Size; ++i) {
    auto& slot = pool.slots[i];
    if (slot.used) {
      CHECK(slot.data != data || allow_intern);
      continue;
    }

    if (HandleIsSet(ret_handle)) {
      ++slot.gen;
      slot.used = true;
      slot.data = data;

      ret_handle = Handle{
          .gen = slot.gen,
          .index = i,
      };
    }
  }

  CHECK(false);
}

template <typename Data, size_t Size>
inline Data& HandleGetData(HandlePool<Data, Size>& pool, Handle handle) {
  CHECK(handle.gen);
  CHECK_LT(handle.index, Size);

  auto& slot = pool.slots[handle.index];
  if (slot.used && handle.gen == slot.gen) {
    return slot.data;
  }

  CHECK(false);
}

template <typename Data, size_t Size>
inline Data HandleRelease(HandlePool<Data, Size>& pool, Handle handle) {
  CHECK(handle.gen);
  CHECK_LT(handle.index, Size);

  auto& slot = pool.slots[handle.index];
  if (slot.used && handle.gen == slot.gen) {
    slot.used = false;
    return slot.data;
  }

  CHECK(false);
}

}  // namespace nyla