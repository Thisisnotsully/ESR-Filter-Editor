// The sums behind a virtualised list (only the rows on screen are drawn) and its scroll bar, and the wheel. The scroll
// bar and the touchpad wheel are the wiki overlay's (DrawScrollbar, HandleMove's thumb drag, HandleWheel).
#pragma once

#include <algorithm>
#include <cstdint>

namespace esrfe::overlay {

// Rows [first, last) that show, at least partly, with `scroll` pixels scrolled off the top.
struct RowRange {
    int first = 0;
    int last = 0;
};

constexpr auto ContentHeight(int count, int rowHeight) -> long long {
    return static_cast<long long>(std::max(0, count)) * std::max(1, rowHeight);
}

constexpr auto MaxScroll(int count, int rowHeight, int viewport) -> int {
    const long long most = ContentHeight(count, rowHeight) - std::max(0, viewport);
    return most > 0 ? static_cast<int>(most) : 0;
}

constexpr auto ClampScroll(int scroll, int count, int rowHeight, int viewport) -> int {
    return std::clamp(scroll, 0, MaxScroll(count, rowHeight, viewport));
}

constexpr auto VisibleRows(int count, int rowHeight, int scroll, int viewport) -> RowRange {
    if (count <= 0 || viewport <= 0) return RowRange{};
    const int h = std::max(1, rowHeight);
    const int first = std::clamp(scroll / h, 0, count);
    const int last = std::clamp((scroll + viewport + h - 1) / h, first, count);
    return RowRange{first, last};
}

// The scroll that brings row `index` fully into view, moving as little as possible.
constexpr auto ScrollToShow(int scroll, int index, int count, int rowHeight, int viewport) -> int {
    if (index < 0 || index >= count) return ClampScroll(scroll, count, rowHeight, viewport);
    const int h = std::max(1, rowHeight);
    const long long top = static_cast<long long>(index) * h;
    const long long bottom = top + h;
    long long next = scroll;
    if (top < next) next = top;
    else if (bottom > next + viewport) next = bottom - viewport;
    return ClampScroll(static_cast<int>(next), count, rowHeight, viewport);
}

// The row at a y offset inside the list's viewport, or -1.
constexpr auto RowAt(int y, int count, int rowHeight, int scroll) -> int {
    if (y < 0) return -1;
    const long long row = (static_cast<long long>(y) + scroll) / std::max(1, rowHeight);
    return row < count ? static_cast<int>(row) : -1;
}

// The thumb of a scroll bar track (the wiki's DrawScrollbar): at least `minThumb` tall, placed in proportion.
struct Thumb {
    int top = 0;
    int height = 0;
    bool shown = false;  // false when everything fits
};

constexpr auto ThumbFor(int trackTop, int trackHeight, int viewport, long long total, int scroll, int minThumb) -> Thumb {
    if (total <= viewport || viewport <= 0 || trackHeight <= 0) return Thumb{};
    const int height = std::min(trackHeight, std::max(minThumb, static_cast<int>(trackHeight * static_cast<long long>(viewport) / total)));
    const long long maxScroll = total - viewport;
    const int top = trackTop + static_cast<int>(static_cast<long long>(trackHeight - height) * std::clamp<long long>(scroll, 0, maxScroll) / maxScroll);
    return Thumb{top, height, true};
}

// The scroll while the thumb is dragged `dy` pixels from where the drag started (the wiki's HandleMove).
constexpr auto ScrollFromDrag(int startScroll, int dy, int trackHeight, int thumbHeight, long long total, int viewport) -> int {
    const int span = trackHeight - thumbHeight;
    const long long maxScroll = std::max<long long>(0, total - viewport);
    if (span <= 0) return static_cast<int>(std::clamp<long long>(startScroll, 0, maxScroll));
    const long long offset = static_cast<long long>(dy) * maxScroll / span;
    return static_cast<int>(std::clamp<long long>(startScroll + offset, 0, maxScroll));
}

// Precision touchpads send many small wheel deltas: they add up to whole notches (120) instead of each rounding to
// none. Turning back drops what was saved.
class WheelAccumulator {
public:
    static constexpr int kNotch = 120;  // WHEEL_DELTA
    // Returns whole notches, up positive.
    auto Add(int delta) -> int {
        if ((remainder_ > 0 && delta < 0) || (remainder_ < 0 && delta > 0)) remainder_ = 0;
        remainder_ += delta;
        const int steps = remainder_ / kNotch;
        remainder_ -= steps * kNotch;
        return steps;
    }
    void Reset() { remainder_ = 0; }

private:
    int remainder_ = 0;
};

}  // namespace esrfe::overlay
