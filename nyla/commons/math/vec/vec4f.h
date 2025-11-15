#pragma once

#define nyla__Vec Vec4f
#define nyla__kVecDimension 4
#define nyla__VecComponentType float
#define nyla__VecSpan std::span<nyla__VecComponentType>
#define nyla__VecSpanConst std::span<const nyla__VecComponentType>

#include "nyla/commons/math/vec/vecbaseinterface.h"

#undef nyla__kVecDimension
#undef nyla__VecComponentType
#undef nyla__VecSpan
#undef nyla__VecSpanConst
#undef nyla__Vec