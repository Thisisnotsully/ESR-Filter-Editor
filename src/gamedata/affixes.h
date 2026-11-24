// properties.txt (property code -> stats), magic affixes, automagic and the
// superior bonuses of qualityitems.txt.
#pragma once

#include "item_types.h"
#include "stats.h"
#include "table.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gamedata {

// One stat a mod gives, with its value range.
struct StatRoll {
    int stat = -1;
    int param = 0;   // the stat's param as the game stores it; -1: any (random skill)
    int min = 0;
    int max = 0;
};

struct PropertyFunc {
    int func = 0;
    int stat = -1;    // -1 where the func has no stat column (dmg-min, ethereal...)
    int set = 0;
    int val = 0;
    bool hasVal = false;
};

struct Property {
    std::string code;
    std::vector<PropertyFunc> funcs;
};

class Properties {
public:
    // Also tells Stats which params mean skills (funcs 10, 11, 19, 22).
    auto Load(const Table& table, Stats& stats, std::vector<std::string>& problems) -> void;

    auto Find(std::string_view code) const -> const Property*;

    // The stats a mod (property code, param, min, max) sets.
    //   1 2 8 13 14 20: stat = min..max (param ignored, as the game does)
    //   3: the previous func's stat range and param again (res-all)
    //   5 6 7: dmg-min, dmg-max, dmg%: the damage stats, which have no stat column
    //   10 21 22 24: stat with param (mod param or skill name, or the val column
    //      for 10/21; 10 stores class * 8 + tab for the mod's class * 3 + tab)
    //   11: skill on event: param = skill << 6 | level (max), value = chance (min)
    //   12 36: a random skill/class: param -1
    //   15 16: only min or only max (elemental damage)
    //   17 18: per level / by time: value = the mod param
    //   19: charges: param = skill << 6 | level (max), value = charges << 8 | charges (min)
    //   23 ethereal, 25 random affix: no stat
    // False if the code is unknown.
    auto Expand(std::string_view code, std::string_view param, int min, int max, const Skills& skills,
                std::vector<StatRoll>& out) const -> bool;

private:
    std::vector<Property> props_;
    std::unordered_map<std::string, int> byCode_;
    int minDamage_ = -1, maxDamage_ = -1, minPercent_ = -1, maxPercent_ = -1;
};

struct AffixMod {
    std::string code;
    std::string param;
    int min = 0;
    int max = 0;
};

enum class AffixKind { Prefix, Suffix, AutoMagic };

struct Affix {
    AffixKind kind = AffixKind::Prefix;
    int row = -1;
    std::string name;        // strings key, shown as the item name part
    int level = 0;           // lowest item level
    int maxLevel = 0;        // 0: no cap
    int levelReq = 0;
    bool spawnable = false;
    bool rare = false;       // can be on rare items
    std::string classSpecific;
    std::string classCode;
    int classLevelReq = 0;
    int frequency = 0;
    int group = 0;
    std::vector<AffixMod> mods;  // mod1..mod3, empty codes left out
    std::vector<int> itypes;     // item type rows
    std::vector<int> etypes;

    // Spawnable with a non-zero frequency.
    auto CanRoll() const -> bool { return spawnable && frequency > 0; }
};

// A row of qualityitems.txt.
struct SuperiorMod {
    int row = -1;
    std::vector<AffixMod> mods;
    std::vector<int> itypes;
    std::vector<int> etypes;
    int frequency = 0;
};

// Loads magicprefix, magicsuffix or automagic.
auto LoadAffixes(const Table& table, AffixKind kind, const ItemTypes& types, std::vector<Affix>& out,
                 std::vector<std::string>& problems) -> void;

// qualityitems.txt. ESR fills itype/etype; the old flag columns (armor,
// weapon, shield, ...) are read only where a row has no itype.
auto LoadSuperior(const Table& table, const ItemTypes& types, std::vector<SuperiorMod>& out,
                  std::vector<std::string>& problems) -> void;

// The item (its two type rows; -1 for none) is one of itypes and none of etypes.
auto TypesMatch(const ItemTypes& types, int typeRow, int type2Row, const std::vector<int>& itypes,
                const std::vector<int>& etypes) -> bool;

}  // namespace gamedata
