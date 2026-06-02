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
auto Distance(Point a, Point b) -> double {
    int dx = a.x - b.x;
    int dy = a.y - b.y;
    return sqrt(dx * dx + dy * dy);
}

auto Midpoint(Point a, Point b) -> Point {
    return {(a.x + b.x) / 2, (a.y + b.y) / 2};
}
