#include "nyla/commons/math/vec/vec3f.h"

#include <cstddef>
#include <span>

#include "absl/log/check.h"

#define nyla__Vec Vec3f

namespace
{

static inline constexpr size_t kVecDimension = 3;
using VecComponentType = float;
using Vec = nyla::nyla__Vec;
using VecSpan = std::span<VecComponentType>;
using VecSpanConst = std::span<const VecComponentType>;

} // namespace

#include "nyla/commons/math/vec/vecbaseimpl.h"

namespace nyla
{

Vec Vec3fCross(const VecSpanConst lhs, const VecSpanConst rhs)
{
    CHECK(lhs.size() >= kVecDimension && rhs.size() >= kVecDimension);

    return {
        lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[2] * rhs[0] - lhs[0] * rhs[2],
        lhs[0] * rhs[1] - lhs[1] * rhs[0],
    };
}

} // namespace nyla

#undef nyla__Vec
