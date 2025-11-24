#pragma once

#include "absl/container/flat_hash_set.h"

namespace nyla {

template <typename E>
using Set = absl::flat_hash_set<E>;

}  // namespace nyla