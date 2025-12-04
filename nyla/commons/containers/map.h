#pragma once

#include "absl/container/flat_hash_map.h"

namespace nyla
{

template <typename K, typename V> using Map = absl::flat_hash_map<K, V>;

} // namespace nyla