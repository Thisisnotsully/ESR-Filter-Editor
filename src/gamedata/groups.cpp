#include "groups.h"

#include <algorithm>

namespace gamedata {

namespace {

// The groups, tried in this order: a base goes to the first group whose
// types it is (by the item type tree). Order matters where types overlap:
// a class ring is a ring before it is a class item; a quest weapon is quest.
struct GroupRule {
    const char* id;
    const char* english;
    const char* nameKey;
    std::vector<const char*> types;
    bool split;  // subgroups from the item types' UICategory
};

const std::vector<GroupRule>& Rules() {
    static const std::vector<GroupRule> rules = {
        {"gold", "Gold", "Gold", {"gold"}, false},
        {"charms", "Charms", "charm", {"char"}, false},
        {"jewels", "Jewels", "jewel", {"jewl"}, false},
        {"jewelry", "Rings & Amulets", "", {"ring", "amul"}, true},
        {"crystals", "Crystals", "", {}, false},  // by the exception below
        {"runes", "Runes", "runes", {"rune"}, false},
        {"gems", "Gems", "gems", {"gem"}, false},
        {"potions", "Potions & Scrolls", "", {"poti", "elix", "scro", "book"}, true},
        {"quest", "Quest, Keys & Organs", "", {"ques", "key", "body", "play"}, false},  // and quest-flagged bases
        {"weapons", "Weapons", "weap", {"weap", "misl"}, true},  // misl: arrows and bolts
        {"armor", "Armor", "armo", {"armo"}, true},
        {"other", "Other", "", {}, false},
    };
    return rules;
}

// ESR exceptions.
// Crystals (xr*, xt*, xl*: Chipped/plain/Flawed Crystal) are item type
// "rune" in misc.txt, but players see them as their own kind.
auto IsCrystal(const Base& b) -> bool { return b.type == "rune" && !b.code.empty() && b.code[0] == 'x'; }

// Subgroups the UICategory column doesn't give: body armor is "armor" there
// (which is also the whole Armor group), orbs and claws are filed under their
// class, tomes and scrolls under two categories.
struct SubgroupOverride {
    const char* type;  // the base is this type or below it
    const char* id;
    const char* english;
    const char* nameKey;
};
const SubgroupOverride Overrides[] = {
    {"tors", "body", "Body Armor", "r_arm"},
    {"orb", "orbs", "Orbs", ""},
    {"h2h", "claws", "Claws", ""},
    {"scro", "scrlt", "Scrolls and Tomes", "scrlt"},
};

auto AddTo(Group& g, const std::string& id, const std::string& english, const std::string& key,
           const std::string& base) -> int {
    for (std::size_t i = 0; i < g.subgroups.size(); ++i) {
        if (g.subgroups[i].id == id) {
            g.subgroups[i].bases.push_back(base);
            return static_cast<int>(i);
        }
    }
    g.subgroups.push_back({id, english, key, {base}});
    return static_cast<int>(g.subgroups.size()) - 1;
}

}  // namespace

auto Group::BaseCount() const -> std::size_t {
    std::size_t n = 0;
    for (const auto& s : subgroups) n += s.bases.size();
    return n;
}

auto Groups::Build(const ItemTypes& types, const Bases& bases) -> void {
    groups_.clear();
    byBase_.clear();
    const auto& rules = Rules();
    for (const auto& r : rules) groups_.push_back({r.id, r.english, r.nameKey, {}});
    const auto is = [&](const Base& b, const char* code) {
        const int row = types.Row(code);
        return types.IsA(b.typeRow, row) || types.IsA(b.type2Row, row);
    };

    for (const auto& b : bases.All()) {
        std::size_t gi = rules.size() - 1;  // Other
        for (std::size_t i = 0; i < rules.size(); ++i) {
            const std::string_view id = rules[i].id;
            bool match = false;
            if (id == "crystals") match = IsCrystal(b);
            else if (id == "runes") match = is(b, "rune") && !IsCrystal(b);
            else if (id == "quest") match = b.quest || std::any_of(rules[i].types.begin(), rules[i].types.end(), [&](const char* t) { return is(b, t); });
            else for (const char* t : rules[i].types) match = match || is(b, t);
            if (match) { gi = i; break; }
        }
        Group& g = groups_[gi];

        std::string id = g.id, english = g.english, key = g.nameKey;
        if (rules[gi].split) {
            bool done = false;
            for (const auto& o : Overrides) {
                if (is(b, o.type)) {
                    id = o.id; english = o.english; key = o.nameKey;
                    done = true;
                    break;
                }
            }
            // Nearest type with a UICategory: the item's own first, then up.
            for (const int row : types.Ancestors(b.typeRow)) {
                if (done) break;
                const ItemType* ty = types.At(row);
                if (!ty->uiCategory.empty()) {
                    id = key = ty->uiCategory;
                    english = ty->name;
                    done = true;
                }
            }
        }
        const int si = AddTo(g, id, english, key, b.code);
        byBase_[b.code] = {static_cast<int>(gi), si};
    }
}

auto Groups::Find(std::string_view id) const -> const Group* {
    for (const auto& g : groups_)
        if (g.id == id) return &g;
    return nullptr;
}

auto Groups::Of(std::string_view baseCode) const -> GroupRef {
    const auto it = byBase_.find(std::string(baseCode));
    return it == byBase_.end() ? GroupRef{} : it->second;
}

auto Groups::Name(const Group& g, const Strings& strings, Language language) -> std::string {
    if (!g.nameKey.empty())
        if (const std::string* t = strings.Find(g.nameKey, language)) return Strings::Plain(*t);
    return g.english;
}

auto Groups::Name(const Subgroup& s, const Strings& strings, Language language) -> std::string {
    if (!s.nameKey.empty())
        if (const std::string* t = strings.Find(s.nameKey, language)) return Strings::Plain(*t);
    return s.english;
}

}  // namespace gamedata
