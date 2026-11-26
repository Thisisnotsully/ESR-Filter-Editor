// "Which items?": the choices the edit-item key offers for an item, from this exact item out to every item of its
// kind. Built from the item's draft (FromItem) and the game's data. Standard C++ only.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "rule_draft.h"
#include "rule_text.h"

namespace esrfe::ui {

struct Scope {
    std::string label;      // "Every magic Grand Charm", "Only Kenji's Winged Arrows"
    RuleDraft draft;        // the items (kind, bases / types / uniques / every, quality, tiers, stats); no action or levels
    std::size_t count = 0;  // how many kinds of item it is; 0 for a whole type or kind (no fixed list)
};

// Narrowest first. `item`: FromItem's draft for the item.
auto ScopesFor(const RuleDraft& item, const gamedata::GameData& game, const DescribeContext& context) -> std::vector<Scope>;

}  // namespace esrfe::ui
