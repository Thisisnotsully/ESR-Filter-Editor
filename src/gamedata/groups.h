// The player-facing grouping of bases for the editor's first step:
// Weapons > Axes, Armor > Helms, Charms, ...
#pragma once

#include "item_types.h"
#include "items.h"
#include "game_strings.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gamedata {

struct Subgroup {
    std::string id;           // "axes", "amazo", "body", "claws"
    std::string english;      // shown where nameKey is empty (the UI translates it)
    std::string nameKey;      // a game strings key, or empty
    std::vector<std::string> bases;  // base codes, file order
};

struct Group {
    std::string id;           // "weapons", "armor", "jewelry", ...
    std::string english;
    std::string nameKey;
    std::vector<Subgroup> subgroups;

    auto BaseCount() const -> std::size_t;
};

struct GroupRef {
    int group = -1;
    int subgroup = -1;
};

class Groups {
public:
    auto Build(const ItemTypes& types, const Bases& bases) -> void;

    auto All() const -> const std::vector<Group>& { return groups_; }
    auto Find(std::string_view id) const -> const Group*;
    auto Of(std::string_view baseCode) const -> GroupRef;

    // The game's text when there is a key, else the English label.
    static auto Name(const Group& g, const Strings& strings, Language language) -> std::string;
    static auto Name(const Subgroup& s, const Strings& strings, Language language) -> std::string;

private:
    std::vector<Group> groups_;
    std::unordered_map<std::string, GroupRef> byBase_;
};

}  // namespace gamedata
