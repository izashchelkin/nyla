#pragma once

#include <span>

#define nyla__Vec Vec2f
#define nyla__kVecDimension 2
#define nyla__VecComponentType float
#define nyla__VecSpan std::span<nyla__VecComponentType>
#define nyla__VecSpanConst std::span<const nyla__VecComponentType>

#include "nyla/commons/math/vec/vecbaseinterface.h"

namespace nyla
{

nyla__Vec VecCross(nyla__VecSpanConst lhs, nyla__VecSpanConst rhs);

} // namespace nyla

#undef nyla__kVecDimension
#undef nyla__VecComponentType
#undef nyla__VecSpan
#undef nyla__VecSpanConst
#undef nyla__Vec
