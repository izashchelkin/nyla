#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace nyla {

template <typename E>
using Set = absl::flat_hash_set<E>;

template <typename K, typename V>
using Map = absl::flat_hash_map<K, V>;

}  // namespace nyla
