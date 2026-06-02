// Test fixture for xcav -- C++ structs and free functions

struct Point {
    int x;
    int y;

    auto GetX() const -> int {
        return x;
    }

    auto GetY() const -> int {
        return y;
    }
};

auto Midpoint(Point a, Point b) -> Point {
    return {(a.x + b.x) / 2, (a.y + b.y) / 2};
}
