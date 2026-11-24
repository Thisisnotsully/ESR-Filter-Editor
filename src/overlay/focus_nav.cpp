#include "focus_nav.hpp"

#include <cstdlib>
#include <limits>

namespace esrfe::overlay {

auto NavNeighbor(const std::vector<Box>& boxes, int current, int dx, int dy, const std::vector<bool>& enabled) -> int {
    const int count = static_cast<int>(boxes.size());
    if (current < 0 || current >= count || (dx == 0 && dy == 0)) return -1;
    const auto ok = [&](int i) { return enabled.empty() || (i < static_cast<int>(enabled.size()) && enabled[static_cast<std::size_t>(i)]); };
    const Box& from = boxes[static_cast<std::size_t>(current)];
    int best = -1;
    long long bestScore = std::numeric_limits<long long>::max();
    for (int i = 0; i < count; ++i) {
        if (i == current || !ok(i)) continue;
        const Box& b = boxes[static_cast<std::size_t>(i)];
        if (b.Empty()) continue;
        long long gap = 0;          // along the direction
        long long misalign = 0;     // across it (0 when the boxes overlap across the direction)
        bool overlap = false;
        if (dx != 0) {
            if (dx > 0 ? (b.left < from.CentreX() || b.CentreX() <= from.CentreX())
                       : (b.right > from.CentreX() || b.CentreX() >= from.CentreX())) continue;
            gap = dx > 0 ? b.left - from.right : from.left - b.right;
            overlap = b.top < from.bottom && b.bottom > from.top;
            if (!overlap) misalign = b.top >= from.bottom ? b.top - from.bottom : from.top - b.bottom;
        } else {
            if (dy > 0 ? (b.top < from.CentreY() || b.CentreY() <= from.CentreY())
                       : (b.bottom > from.CentreY() || b.CentreY() >= from.CentreY())) continue;
            gap = dy > 0 ? b.top - from.bottom : from.top - b.bottom;
            overlap = b.left < from.right && b.right > from.left;
            if (!overlap) misalign = b.left >= from.right ? b.left - from.right : from.left - b.right;
        }
        if (gap < 0) gap = 0;
        // Among overlapping boxes, the nearest; a tie goes to the one whose centre lines up best.
        const long long centreOff = dx != 0 ? std::llabs(static_cast<long long>(b.CentreY()) - from.CentreY())
                                            : std::llabs(static_cast<long long>(b.CentreX()) - from.CentreX());
        const long long score = overlap ? gap * 4096 + centreOff
                                        : (1LL << 40) + (gap + misalign * 3) * 4096 + centreOff;
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

auto TabStep(const std::vector<bool>& enabled, int current, int direction) -> int {
    const int count = static_cast<int>(enabled.size());
    if (count == 0) return -1;
    const int dir = direction < 0 ? -1 : 1;
    int at = current < 0 || current >= count ? (dir > 0 ? -1 : count) : current;
    for (int step = 0; step < count; ++step) {
        at = ((at + dir) % count + count) % count;
        if (enabled[static_cast<std::size_t>(at)]) return at;
    }
    return -1;
}

}  // namespace esrfe::overlay
