#include <cstddef>
#include <span>

#include "nyla/commons/math/vec/vec4f.h"

#define nyla__Vec Vec4f

namespace {

static inline constexpr size_t kVecDimension = 4;
using VecComponentType = float;
using Vec = nyla::nyla__Vec;
using VecSpan = std::span<VecComponentType>;
using VecSpanConst = std::span<const VecComponentType>;

}  // namespace

#include "nyla/commons/math/vec/vecbaseimpl.h"

#undef nyla__Vec