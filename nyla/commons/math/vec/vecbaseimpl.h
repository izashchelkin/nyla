#ifdef nyla__Vec
#define NYLA__CAT(a, b) a##b
#define NYLA__XCAT(a, b) NYLA__CAT(a, b)
#define NYLA__VEC(suffix) NYLA__XCAT(nyla__Vec, suffix)

#include <cmath>
#include <complex>

#include "absl/log/check.h"
#include "nyla/commons/memory/temp.h"

namespace nyla
{

Vec::operator VecSpanConst() const &
{
    return VecSpanConst{v, kVecDimension};
}
Vec::operator VecSpan() &
{
    return VecSpan{v, kVecDimension};
}
Vec::operator VecSpanConst() const &&
{
    return Tmake(std::move(*this));
};
Vec::operator VecSpan() &&
{
    return Tmake(std::move(*this));
};

auto nyla__Vec::operator[](size_t i) const -> const VecComponentType &
{
    CHECK(i < kVecDimension);
    return v[i];
}
auto nyla__Vec::operator[](size_t i) -> VecComponentType &
{
    CHECK(i < kVecDimension);
    return v[i];
}

auto NYLA__VEC(Neg)(VecSpanConst rhs) -> Vec
{
    CHECK(rhs.size() >= kVecDimension);

    return Vec{-rhs[0], -rhs[1]};
}

auto NYLA__VEC(Sum)(VecSpanConst lhs, VecSpanConst rhs) -> Vec
{
    CHECK(lhs.size() >= kVecDimension && rhs.size() >= kVecDimension);

    Vec ret;
    for (size_t i = 0; i < kVecDimension; ++i)
    {
        ret[i] = lhs[i] + rhs[i];
    }
    return ret;
}

void NYLA__VEC(Add)(VecSpan lhs, VecSpanConst rhs)
{
    CHECK(lhs.size() >= kVecDimension && rhs.size() >= kVecDimension);

    for (size_t i = 0; i < kVecDimension; ++i)
    {
        lhs[i] += rhs[i];
    }
}

auto NYLA__VEC(Dif)(VecSpanConst lhs, VecSpanConst rhs) -> Vec
{
    CHECK(lhs.size() >= kVecDimension && rhs.size() >= kVecDimension);

    return Vec{lhs[0] - rhs[0], lhs[1] - rhs[1]};
}

void NYLA__VEC(Sub)(VecSpan lhs, VecSpanConst rhs)
{
    CHECK(lhs.size() >= kVecDimension && rhs.size() >= kVecDimension);

    for (size_t i = 0; i < kVecDimension; ++i)
    {
        lhs[i] -= rhs[i];
    }
}

auto NYLA__VEC(Mul)(VecSpanConst lhs, VecComponentType scalar) -> Vec
{
    CHECK(lhs.size() >= kVecDimension);

    return Vec{lhs[0] * scalar, lhs[1] * scalar};
}

void NYLA__VEC(Scale)(VecSpan lhs, VecComponentType scalar)
{
    CHECK(lhs.size() >= kVecDimension);

    for (size_t i = 0; i < kVecDimension; ++i)
    {
        lhs[i] *= scalar;
    }
}

auto NYLA__VEC(Div)(VecSpanConst lhs, VecComponentType scalar) -> Vec
{
    CHECK(lhs.size() >= kVecDimension);

    return Vec{lhs[0] / scalar, lhs[1] / scalar};
}

void NYLA__VEC(ScaleDown)(VecSpan lhs, VecComponentType scalar)
{
    CHECK(lhs.size() >= kVecDimension);

    for (size_t i = 0; i < kVecDimension; ++i)
    {
        lhs[i] /= scalar;
    }
}

auto NYLA__VEC(Eq)(VecSpanConst lhs, VecSpanConst rhs) -> bool
{
    CHECK(lhs.size() >= kVecDimension && rhs.size() >= kVecDimension);

    for (size_t i = 0; i < kVecDimension; ++i)
    {
        if (lhs[i] != rhs[i])
            return false;
    }
    return true;
}

auto NYLA__VEC(Len)(VecSpanConst rhs) -> VecComponentType
{
    VecComponentType ret{};
    for (size_t i = 0; i < kVecDimension; ++i)
    {
        ret += rhs[i] * rhs[i];
    }
    return std::sqrt(ret);
}

auto NYLA__VEC(Resized)(VecSpanConst rhs, VecComponentType len) -> Vec
{
    return NYLA__VEC(Mul)(rhs, len / NYLA__VEC(Len)(rhs));
}

auto NYLA__VEC(Norm)(VecSpanConst rhs) -> Vec
{
    return NYLA__VEC(Div)(rhs, NYLA__VEC(Len)(rhs));
}

auto NYLA__VEC(Dot)(VecSpanConst lhs, VecSpanConst rhs) -> VecComponentType
{
    CHECK(lhs.size() >= kVecDimension && rhs.size() >= kVecDimension);

    VecComponentType ret{};
    for (size_t i = 0; i < kVecDimension; ++i)
    {
        ret += lhs[i] * rhs[i];
    }
    return ret;
}

auto NYLA__VEC(Apply)(VecSpanConst lhs, std::complex<VecComponentType> comp) -> Vec
{
    CHECK(lhs.size() >= kVecDimension);

    Vec ret{};

    {
        auto res = std::complex<VecComponentType>{lhs[0], lhs[1]} * comp;
        ret[0] = res.real();
        ret[1] = res.imag();
    }

    for (size_t i = 2; i < kVecDimension; ++i)
    {
        ret[i] = lhs[i];
    }

    return ret;
}

} // namespace nyla

#undef NYLA__VEC
#undef NYLA__XCAT
#undef NYLA__CAT
#endif