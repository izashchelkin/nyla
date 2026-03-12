#pragma once

#include <type_traits>
#include <utility>

namespace nyla
{

template <class F> class Cleanup
{
  public:
    explicit Cleanup(F fn) noexcept(std::is_nothrow_move_constructible_v<F>) : m_Fn(std::move(fn))
    {
    }

    Cleanup(Cleanup &&) noexcept(std::is_nothrow_move_constructible_v<F>) = default;
    auto operator=(Cleanup &&) -> Cleanup & = delete;

    Cleanup(const Cleanup &) = delete;
    auto operator=(const Cleanup &) -> Cleanup & = delete;

    ~Cleanup() noexcept
    {
        if (m_Active)
            m_Fn();
    }

    void Dismiss() noexcept
    {
        m_Active = false;
    }

  private:
    F m_Fn;
    bool m_Active{true};
};

template <class F> Cleanup(F) -> Cleanup<F>;

} // namespace nyla