// Test fixture for xcav -- nested structures (namespace, class, enum, template)

namespace math {

template<typename T>
class Calculator {
public:
    auto Add(T a, T b) -> T {
        return a + b;
    }

    auto Subtract(T a, T b) -> T {
        return a - b;
    }
};

auto Execute(Operation op, int a, int b) -> int {
    if (op == Operation::Add) return a + b;
    return a - b;
}

} // namespace math
