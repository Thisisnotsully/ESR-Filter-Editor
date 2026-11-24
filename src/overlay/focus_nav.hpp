// Where the D-pad and the arrow keys move the focus. The wiki overlay had one rule per zone (tabs, list, cards, chips,
// ...); a form needs one rule for any layout, so this is the general version: the nearest box in the pressed direction,
// preferring boxes that line up with the current one. pad::Neighbor (pad_logic.hpp) is still the rule for a grid of
// tabs.
#pragma once

#include <vector>

#include "geometry.hpp"

namespace esrfe::overlay {

// The box a direction leads to from `current`, or -1. dx/dy is one of (-1,0) (1,0) (0,-1) (0,1).
//   * Only boxes past the current one in that direction count: for Right, a box whose left edge is at or past the
//     current box's centre and whose centre is right of it.
//   * Boxes that overlap the current one across the direction (same row for Left/Right, same column for Up/Down) come
//     first, nearest first; then the others by distance, with misalignment weighed three times.
// `enabled` (may be empty: all enabled) leaves out boxes that cannot take the focus.
auto NavNeighbor(const std::vector<Box>& boxes, int current, int dx, int dy, const std::vector<bool>& enabled = {}) -> int;

// Tab / Shift+Tab: the next enabled index after `current` in `direction` (+1/-1), wrapping; -1 when none is enabled.
auto TabStep(const std::vector<bool>& enabled, int current, int direction) -> int;

}  // namespace esrfe::overlay
