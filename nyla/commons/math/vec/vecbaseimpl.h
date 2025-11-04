#ifdef nyla__Vec
#define NYLA__CAT(a, b) a##b
#define NYLA__XCAT(a, b) NYLA__CAT(a, b)
#define NYLA__VEC(suffix) NYLA__XCAT(nyla__Vec, suffix)

#include <cmath>
#include <complex>

#include "absl/log/check.h"
#include "nyla/commons/memory/tnew.h"

namespace nyla {

Vec::operator VecSpanConst() const& { return VecSpanConst{v, kVecDimension}; }
Vec::operator VecSpan() & { return VecSpan{v, kVecDimension}; }
Vec::operator VecSpanConst() const&& { return *Tnew_from(std::move(*this)); };
Vec::operator VecSpan() && { return *Tnew_from(std::move(*this)); };

const VecComponentType& nyla__Vec::operator[](size_t i) const {
  CHECK(i < kVecDimension);
  return v[i];
}
VecComponentType& nyla__Vec::operator[](size_t i) {
  CHECK(i < kVecDimension);
  return v[i];
}

Vec NYLA__VEC(Neg)(VecSpanConst rhs) {
  CHECK(rhs.size() >= kVecDimension);

  return Vec{-rhs[0], -rhs[1]};
}

Vec NYLA__VEC(Sum)(VecSpanConst lhs, VecSpanConst rhs) {
  CHECK(lhs.size() >= kVecDimension && rhs.size() >= kVecDimension);

  Vec ret;
  for (size_t i = 0; i < kVecDimension; ++i) {
    ret[i] = lhs[i] + rhs[i];
  }
  return ret;
}

void NYLA__VEC(Add)(VecSpan lhs, VecSpanConst rhs) {
  CHECK(lhs.size() >= kVecDimension && rhs.size() >= kVecDimension);

  for (size_t i = 0; i < kVecDimension; ++i) {
    lhs[i] += rhs[i];
  }
}

Vec NYLA__VEC(Dif)(VecSpanConst lhs, VecSpanConst rhs) {
  CHECK(lhs.size() >= kVecDimension && rhs.size() >= kVecDimension);

  return Vec{lhs[0] - rhs[0], lhs[1] - rhs[1]};
}

void NYLA__VEC(Sub)(VecSpan lhs, VecSpanConst rhs) {
  CHECK(lhs.size() >= kVecDimension && rhs.size() >= kVecDimension);

  for (size_t i = 0; i < kVecDimension; ++i) {
    lhs[i] -= rhs[i];
  }
}

Vec NYLA__VEC(Mul)(VecSpanConst lhs, VecComponentType scalar) {
  CHECK(lhs.size() >= kVecDimension);

  return Vec{lhs[0] * scalar, lhs[1] * scalar};
}

void NYLA__VEC(Scale)(VecSpan lhs, VecComponentType scalar) {
  CHECK(lhs.size() >= kVecDimension);

  for (size_t i = 0; i < kVecDimension; ++i) {
    lhs[i] *= scalar;
  }
}

Vec NYLA__VEC(Div)(VecSpanConst lhs, VecComponentType scalar) {
  CHECK(lhs.size() >= kVecDimension);

  return Vec{lhs[0] / scalar, lhs[1] / scalar};
}

void NYLA__VEC(ScaleDown)(VecSpan lhs, VecComponentType scalar) {
  CHECK(lhs.size() >= kVecDimension);

  for (size_t i = 0; i < kVecDimension; ++i) {
    lhs[i] /= scalar;
  }
}

bool NYLA__VEC(Eq)(VecSpanConst lhs, VecSpanConst rhs) {
  CHECK(lhs.size() >= kVecDimension && rhs.size() >= kVecDimension);

  for (size_t i = 0; i < kVecDimension; ++i) {
    if (lhs[i] != rhs[i]) return false;
  }
  return true;
}

VecComponentType NYLA__VEC(Len)(VecSpanConst rhs) {
  VecComponentType ret{};
  for (size_t i = 0; i < kVecDimension; ++i) {
    ret += rhs[i] * rhs[i];
  }
  return std::sqrt(ret);
}

Vec NYLA__VEC(Norm)(VecSpanConst rhs) {
  return NYLA__VEC(Div)(rhs, NYLA__VEC(Len)(rhs));
}

VecComponentType NYLA__VEC(Dot)(VecSpanConst lhs, VecSpanConst rhs) {
  CHECK(lhs.size() >= kVecDimension && rhs.size() >= kVecDimension);

  VecComponentType ret{};
  for (size_t i = 0; i < kVecDimension; ++i) {
    ret += lhs[i] * rhs[i];
  }
  return ret;
}

Vec NYLA__VEC(Apply)(VecSpanConst lhs, std::complex<VecComponentType> comp) {
  CHECK(lhs.size() >= kVecDimension);

  Vec ret{};

  {
    auto res = std::complex<VecComponentType>{lhs[0], lhs[1]} * comp;
    ret[0] = res.real();
    ret[1] = res.imag();
  }

  for (size_t i = 2; i < kVecDimension; ++i) {
    ret[i] = lhs[i];
  }

  return ret;
}

}  // namespace nyla

#undef NYLA__VEC
#undef NYLA__XCAT
#undef NYLA__CAT
#endif
