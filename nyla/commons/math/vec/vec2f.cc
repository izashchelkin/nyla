#include "nyla/commons/math/vec/vec2f.h"

#include <cstddef>
#include <span>

#define nyla__Vec Vec2f

namespace
{

static inline constexpr size_t kVecDimension = 2;
using VecComponentType = float;
using Vec = nyla::nyla__Vec;
using VecSpan = std::span<VecComponentType>;
using VecSpanConst = std::span<const VecComponentType>;

} // namespace

#include "nyla/commons/math/vec/vecbaseimpl.h"

#undef nyla__Vec
