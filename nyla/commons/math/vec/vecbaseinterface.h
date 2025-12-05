#ifdef nyla__Vec
#define NYLA__CAT(a, b) a##b
#define NYLA__XCAT(a, b) NYLA__CAT(a, b)
#define NYLA__VEC(suffix) NYLA__XCAT(nyla__Vec, suffix)

#include <complex>

namespace nyla
{

struct nyla__Vec
{
    nyla__VecComponentType v[nyla__kVecDimension];

    operator nyla__VecSpanConst() const &;
    operator nyla__VecSpan() &;
    operator nyla__VecSpan() &&;
    operator nyla__VecSpanConst() const &&;

    auto operator[](size_t i) const -> const nyla__VecComponentType &;
    auto operator[](size_t i) -> nyla__VecComponentType &;
};

auto NYLA__VEC(Neg)(nyla__VecSpanConst rhs) -> nyla__Vec;

auto NYLA__VEC(Sum)(nyla__VecSpanConst lhs, nyla__VecSpanConst rhs) -> nyla__Vec;

void NYLA__VEC(Add)(nyla__VecSpan lhs, nyla__VecSpanConst rhs);

auto NYLA__VEC(Dif)(nyla__VecSpanConst lhs, nyla__VecSpanConst rhs) -> nyla__Vec;

void NYLA__VEC(Sub)(nyla__VecSpan lhs, nyla__VecSpanConst rhs);

auto NYLA__VEC(Mul)(nyla__VecSpanConst lhs, nyla__VecComponentType scalar) -> nyla__Vec;

void NYLA__VEC(Scale)(nyla__VecSpan lhs, nyla__VecComponentType scalar);

auto NYLA__VEC(Div)(nyla__VecSpanConst lhs, nyla__VecComponentType scalar) -> nyla__Vec;

void NYLA__VEC(ScaleDown)(nyla__VecSpan lhs, nyla__VecComponentType scalar);

auto NYLA__VEC(Eq)(nyla__VecSpanConst lhs, nyla__VecSpanConst rhs) -> bool;

auto NYLA__VEC(Len)(nyla__VecSpanConst rhs) -> nyla__VecComponentType;

auto NYLA__VEC(Resized)(nyla__VecSpanConst rhs, nyla__VecComponentType len) -> nyla__Vec;

auto NYLA__VEC(Norm)(nyla__VecSpanConst rhs) -> nyla__Vec;

auto NYLA__VEC(Dot)(nyla__VecSpanConst lhs, nyla__VecSpanConst rhs) -> nyla__VecComponentType;

auto NYLA__VEC(Apply)(nyla__VecSpanConst lhs, std::complex<nyla__VecComponentType> comp) -> nyla__Vec;

} // namespace nyla

#undef NYLA__VEC
#undef NYLA__XCAT
#undef NYLA__CAT
#endif
