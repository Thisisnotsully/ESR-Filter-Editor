// Item bases (weapons, armor, misc), uniques, set items and sets.
#pragma once

#include "item_types.h"
#include "game_strings.h"
#include "table.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gamedata {

enum class BaseKind { Weapon, Armor, Misc };

// Normal / exceptional / elite. None for misc items.
enum class Tier { None, Normal, Exceptional, Elite };

// Which qualities an item of the base can have. See gamedata.cpp for how
// each is worked out (WorkOutRarities).
struct Rarities {
    bool normal = false;    // plain (white or grey) items
    bool superior = false;  // with a qualityitems.txt bonus
    bool magic = false;
    bool rare = false;
    bool set = false;
    bool unique = false;
};

struct Base {
    std::string code;
    std::string name;        // the name column (English, for tools)
    std::string nameKey;     // strings key: namestr, else code
    BaseKind kind = BaseKind::Misc;
    int fileRow = -1;        // row in its own file
    std::string type, type2;
    int typeRow = -1, type2Row = -1;
    std::string normCode, uberCode, ultraCode;
    Tier tier = Tier::None;
    bool mythical = false;      // ESR Mythical weapon/armor: own norm/uber/ultra code, level 96
    bool mythicalMisc = false;  // Mythical Amulet/Ring/Jewel: misc, own codes, level 100
    int level = 0;
    int levelReq = 0;
    int gemSockets = 0;      // gemsockets column
    int autoPrefix = 0;      // "auto prefix": automagic group, 0 for none
    int maxSockets = 0;      // gemsockets capped by the item type at high item level
    bool spawnable = false;
    bool quest = false;
    Rarities rarities;
};

class Bases {
public:
    // weapons.txt, armor.txt, misc.txt, in that order.
    auto Load(const Table& weapons, const Table& armor, const Table& misc, const ItemTypes& types,
              std::vector<std::string>& problems) -> void;

    auto All() const -> const std::vector<Base>& { return bases_; }
    auto Mutable() -> std::vector<Base>& { return bases_; }
    auto Find(std::string_view code) const -> const Base*;

    // From the strings; the name column if the key is missing.
    auto Name(const Base& base, const Strings& strings, Language language) const -> std::string;

    // Item type cap and base sockets at an item level.
    auto MaxSocketsAt(const Base& base, int itemLevel, const ItemTypes& types) const -> int;

private:
    std::vector<Base> bases_;
    std::unordered_map<std::string, int> byCode_;
};

struct Unique {
    int row = -1;            // uniqueitems.txt row, as the loot filter's uniqueOrSetIds
    std::string key;         // index column, also the strings key
    std::string code;        // base
    int level = 0;
    int levelReq = 0;
    bool spawnable = false;
    bool disabled = false;
    bool onMythical = false;
};

struct SetItem {
    int row = -1;            // setitems.txt row
    std::string key;         // index column, also the strings key
    std::string setKey;      // set column = sets.txt index
    int setRow = -1;
    std::string code;
    int level = 0;
    int levelReq = 0;
    bool spawnable = false;
    bool disabled = false;
    bool onMythical = false;
};

struct Set {
    int row = -1;
    std::string key;         // index column
    std::string nameKey;     // name column
    std::vector<int> items;  // setitems rows
};

class Uniques {
public:
    auto Load(const Table& table, const Bases& bases, std::vector<std::string>& problems) -> void;
    auto All() const -> const std::vector<Unique>& { return items_; }
    auto At(int row) const -> const Unique*;
    auto Name(const Unique& u, const Strings& strings, Language language) const -> std::string;
    // Rows on the base.
    auto ForBase(std::string_view code) const -> std::vector<int>;

private:
    std::vector<Unique> items_;
};

class Sets {
public:
    auto Load(const Table& setItems, const Table& sets, const Bases& bases, std::vector<std::string>& problems) -> void;
    auto Items() const -> const std::vector<SetItem>& { return items_; }
    auto All() const -> const std::vector<Set>& { return sets_; }
    auto Item(int row) const -> const SetItem*;
    auto SetOf(const SetItem& item) const -> const Set*;
    auto Name(const SetItem& item, const Strings& strings, Language language) const -> std::string;
    auto SetName(const Set& set, const Strings& strings, Language language) const -> std::string;
    auto ForBase(std::string_view code) const -> std::vector<int>;

private:
    std::vector<SetItem> items_;
    std::vector<Set> sets_;
};

}  // namespace gamedata
