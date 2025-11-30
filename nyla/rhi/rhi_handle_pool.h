#pragma once

#include <cstddef>
#include <cstdint>

#include "absl/log/check.h"

namespace nyla {

struct RhiHandle {
  uint32_t gen;
  uint32_t index;
};

inline bool RhiHandleIsSet(RhiHandle handle) {
  return handle.gen;
}

namespace rhi_internal {

template <typename Data, size_t Size>
struct RhiHandlePool {
  struct {
    Data data;
    uint32_t gen;
    bool used;
  } slots[Size];
};

template <typename Data, size_t Size>
inline RhiHandle RhiHandleAcquire(RhiHandlePool<Data, Size>& pool, Data data, bool allow_intern = false) {
  RhiHandle ret_handle;

  for (uint32_t i = 0; i < Size; ++i) {
    auto& slot = pool.slots[i];
    if (slot.used) {
      CHECK(slot.data != data || allow_intern);
      continue;
    }

    if (RhiHandleIsSet(ret_handle)) {
      ++slot.gen;
      slot.used = true;
      slot.data = data;

      ret_handle = RhiHandle{
          .gen = slot.gen,
          .index = i,
      };
    }
  }

  CHECK(false);
}

template <typename Data, size_t Size>
inline Data& RhiHandleGetData(RhiHandlePool<Data, Size>& pool, RhiHandle handle) {
  CHECK(handle.gen);
  CHECK_LT(handle.index, Size);

  auto& slot = pool.slots[handle.index];
  if (slot.used && handle.gen == slot.gen) {
    return slot.data;
  }

  CHECK(false);
}

template <typename Data, size_t Size>
inline Data RhiHandleRelease(RhiHandlePool<Data, Size>& pool, RhiHandle handle) {
  CHECK(handle.gen);
  CHECK_LT(handle.index, Size);

  auto& slot = pool.slots[handle.index];
  if (slot.used && handle.gen == slot.gen) {
    slot.used = false;
    return slot.data;
  }

  CHECK(false);
}

}  // namespace rhi_internal

}  // namespace nyla