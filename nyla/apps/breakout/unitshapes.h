#pragma once

#include <complex>
#include <cstddef>

#include "absl/log/check.h"
#include "nyla/commons/math/math.h"

namespace nyla
{

inline void GenUnitCircle(size_t n, auto consumer)
{
    CHECK(n > 4);

    const float theta = 2.f * pi / n;
    std::complex<float> r = 1.f;

    for (size_t i = 0; i < n; ++i)
    {
        consumer(0.f, 0.f);
        consumer(r.real(), r.imag());

        using namespace std::complex_literals;
        r *= std::cos(theta) + std::sin(theta) * 1if;

        consumer(r.real(), r.imag());
    }
}

inline void GenUnitRect(auto consumer)
{
    const float x = -.5f;
    const float y = .5f;

    consumer(x, y);
    consumer(x + 1.f, y + -1.f);
    consumer(x + 1.f, y);

    consumer(x, y);
    consumer(x, y + -1.f);
    consumer(x + 1.f, y + -1.f);
}

} // namespace nyla