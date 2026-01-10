#pragma once

#include <type_traits>
#include <utility>

namespace nyla
{

template <class F> class Cleanup
{
  public:
    explicit Cleanup(F fn) noexcept(std::is_nothrow_move_constructible_v<F>) : m_fn(std::move(fn))
    {
    }

    Cleanup(Cleanup &&) noexcept(std::is_nothrow_move_constructible_v<F>) = default;
    Cleanup &operator=(Cleanup &&) = delete;

    Cleanup(const Cleanup &) = delete;
    Cleanup &operator=(const Cleanup &) = delete;

    ~Cleanup() noexcept
    {
        if (m_active)
            m_fn();
    }

    void dismiss() noexcept
    {
        m_active = false;
    }

  private:
    F m_fn;
    bool m_active{true};
};

template <class F> Cleanup(F) -> Cleanup<F>;

} // namespace nyla