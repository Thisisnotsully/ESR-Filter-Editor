// Rectangles in client pixels. No Windows headers: the toolkit and its tests use this; the Win32 side converts to RECT.
#pragma once

#include <algorithm>

namespace esrfe::overlay {

struct Box {
    int left = 0, top = 0, right = 0, bottom = 0;

    [[nodiscard]] constexpr auto Width() const -> int { return right - left; }
    [[nodiscard]] constexpr auto Height() const -> int { return bottom - top; }
    [[nodiscard]] constexpr auto Empty() const -> bool { return right <= left || bottom <= top; }
    [[nodiscard]] constexpr auto Contains(int x, int y) const -> bool {
        return x >= left && x < right && y >= top && y < bottom;
    }
    [[nodiscard]] constexpr auto CentreX() const -> int { return (left + right) / 2; }
    [[nodiscard]] constexpr auto CentreY() const -> int { return (top + bottom) / 2; }
    [[nodiscard]] constexpr auto Inflated(int by) const -> Box { return Box{left - by, top - by, right + by, bottom + by}; }
    [[nodiscard]] constexpr auto Offset(int dx, int dy) const -> Box {
        return Box{left + dx, top + dy, right + dx, bottom + dy};
    }
    [[nodiscard]] constexpr auto Intersect(const Box& other) const -> Box {
        return Box{std::max(left, other.left), std::max(top, other.top), std::min(right, other.right),
                   std::min(bottom, other.bottom)};
    }
    constexpr auto operator==(const Box&) const -> bool = default;
};

constexpr auto MakeBox(int left, int top, int width, int height) -> Box {
    return Box{left, top, left + width, top + height};
}

}  // namespace esrfe::overlay
