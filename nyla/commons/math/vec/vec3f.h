#pragma once

#include <span>

#define nyla__Vec Vec3f
#define nyla__kVecDimension 3
#define nyla__VecComponentType float
#define nyla__VecSpan std::span<nyla__VecComponentType>
#define nyla__VecSpanConst std::span<const nyla__VecComponentType>

#include "nyla/commons/math/vec/vecbaseinterface.h"

namespace nyla
{

#define NYLA__CAT(a, b) a##b
#define NYLA__XCAT(a, b) NYLA__CAT(a, b)
#define NYLA__VEC(suffix) NYLA__XCAT(nyla__Vec, suffix)

nyla__Vec NYLA__VEC(Cross)(nyla__VecSpanConst lhs, nyla__VecSpanConst rhs);

#undef NYLA__VEC
#undef NYLA__XCAT
#undef NYLA__CAT

} // namespace nyla

#undef nyla__kVecDimension
#undef nyla__VecComponentType
#undef nyla__VecSpan
#undef nyla__VecSpanConst
#undef nyla__Vec