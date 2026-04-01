#pragma once

#include <cstdint>
#include <type_traits>

namespace nyla
{

template <typename T> void IteratorAdvance(T &iter);
template <typename T> [[nodiscard]] auto IteratorDistance(T lhs, T rhs) -> uint64_t;

template <typename T>
void IteratorAdvance(T iter)
    requires(std::is_pointer_v<T>())
{
    ++iter;
}

template <typename T>
[[nodiscard]]
auto IteratorDistance(T lhs, T rhs) -> uint64_t
    requires(std::is_pointer_v<T>())
{
    return rhs - lhs;
}

template <typename T, typename Elem> void Erase(T &data, const Elem &elem)
{
    auto it = data.begin();
    auto write = data.begin();
    auto end = data.end();

    while (it != end)
    {
        if (*it != elem)
        {
            *write = *it;
            IteratorAdvance(write);
        }
        IteratorAdvance(it);
    }

    data.Resize(data.Size() - IteratorDistance(write, end));
}

} // namespace nyla
