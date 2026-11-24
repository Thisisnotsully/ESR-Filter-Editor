// A player's new rule while they build it, step by step:
//   1 what happens: hide, show (on the levels chosen), always show (every level);
//   2 what kind of item: white and superior bases, magic, rare, unique (Ancient Coupons with them), set, other
//     (runes, gems, potions, ...);
//   3 which ones: bases (and their tiers, Mythicals with / without / only, white or superior, the superior or magic
//     bonuses they can roll), or the unique and set items themselves;
//   4 the filter levels (not for always show);
//   5 the look when it shows: border, background, name colour, light beam, drop sound, chat line.
// ToRules writes the loot filter rules (one, or two when uniques and coupons are picked together: a rule's conditions
// all have to hold, and the two are told apart differently); FromRule reads one back to change it; FromItem starts
// from an item the player pointed at.
//
// Standard C++ (tested on Linux with the mod's data).
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "changes.h"
#include "gamedata.h"
#include "json.h"

namespace esrfe::ui {

enum class DraftAction { None, Hide, Show, AlwaysShow };
enum class ItemKind { None, Base, Magic, Rare, Unique, Set, Other };

struct Rgb {
    int r = 0, g = 0, b = 0;
    friend auto operator==(const Rgb&, const Rgb&) -> bool = default;
};

// Colours offered as swatches (border, background, beam); any other colour comes from the colour picker.
struct NamedColor {
    const char* english;
    Rgb rgb;
};
auto Swatches() -> const std::vector<NamedColor>&;

// The game's own name colours (ÿc codes), the only ones a name can have.
struct NameColor {
    const char* english;
    char code;  // after "ÿc"
    Rgb rgb;    // as drawn, for the preview
};
auto NameColors() -> const std::vector<NameColor>&;

struct Look {
    std::optional<Rgb> border;
    std::optional<Rgb> background;
    int nameColor = -1;  // NameColors index, -1: the game's colour for the item
    bool beam = false;
    Rgb beamColor{255, 200, 0};
    std::string sound;   // a file of the filter's sounds folder, "" for none
    bool chat = false;
    std::string chatText = "{name}";  // {name}: the item's name
    int chatColor = 4;   // NameColors index
    [[nodiscard]] auto Any() const -> bool {
        return border || background || nameColor >= 0 || beam || !sound.empty() || chat;
    }
    friend auto operator==(const Look&, const Look&) -> bool = default;
};

struct StatPick {
    int stat = -1;
    int param = 0;
    int atLeast = 0;
};

struct RuleDraft {
    DraftAction action = DraftAction::None;
    ItemKind kind = ItemKind::None;
    std::vector<std::string> bases;      // Base, Magic, Rare, Other: base codes
    std::set<gamedata::Tier> tiers;      // empty: every tier
    enum class Mythicals { Include, Leave, Only } mythical = Mythicals::Include;
    std::set<int> baseQualities;         // Base: 2 white, 3 superior; empty: both
    std::vector<int> uniques;            // Unique: uniqueitems rows
    std::vector<std::string> coupons;    // Unique: Ancient Coupon codes
    std::vector<int> setItems;           // Set: setitems rows
    std::vector<StatPick> stats;         // superior-only bases, or magic items
    std::vector<std::int64_t> levels;    // empty: every level (always show: always)
    Look look;
};

auto KindChoices() -> const std::vector<std::pair<ItemKind, const char*>>&;

// An Ancient Coupon: a misc item named "(<base>, <unique>)\nAncient Coupon", counted as unique, tiered by its base.
struct Coupon {
    std::string code;
    std::string base;    // the base's code, "" when its name is not a base's
    gamedata::Tier tier = gamedata::Tier::None;
};
auto Coupons(const gamedata::GameData& game) -> const std::vector<Coupon>&;
auto IsCoupon(const gamedata::GameData& game, const std::string& code) -> bool;

// The bases a kind lists (the items step): weapons and armor for white and superior, everything that comes magic or
// rare for those, runes / gems / potions / keys and the like for other.
auto KindAllows(ItemKind kind, const gamedata::GameData& game, const gamedata::Base& base) -> bool;

// The tier a unique or set item has through its base (None for rings, amulets, charms, jewels).
auto TierOfUnique(const gamedata::GameData& game, const std::string& baseCode) -> gamedata::Tier;

// A stat the chosen items can roll, for the bonuses.
struct StatOption {
    int stat = -1;
    int param = 0;
    int min = 0;  // what "at least" can be (above 0)
    int max = 0;
    std::string label;  // "+4 to Teleport" (at the highest roll), plain text
};

struct DraftOffer {
    std::set<gamedata::Tier> tierChoices;  // the tiers among the chosen bases
    bool tiers = false;      // something to choose (two tiers, or a Mythical among them)
    bool mythicals = false;
    bool qualities = false;  // white and superior: which of the two
    bool statsPossible = false;
    std::string statsWhy;    // English, when not
    std::vector<StatOption> stats;
};

auto ChosenBases(const RuleDraft& draft, const gamedata::GameData& game) -> std::vector<std::string>;
auto Offer(const RuleDraft& draft, const gamedata::GameData& game) -> DraftOffer;
// Drops choices the items no longer allow, and what the kind does not use.
void Tidy(RuleDraft& draft, const gamedata::GameData& game);

// Hide and plain show rules go "top"; a shown rule with a look goes "end" with keepShown (its colours last, never
// hidden). The title is "<summary> | <action>"; the session puts the U### id in front.
auto ToRules(const RuleDraft& draft, const gamedata::GameData& game, const std::string& summary) -> std::vector<edit::OwnRule>;
auto FromRule(const document::Value& rule, const gamedata::GameData& game, RuleDraft& out) -> bool;

struct ItemStat {
    int stat = -1;
    int param = 0;
    int value = 0;
};
// The item the player points at: its kind, the item itself (base, unique, set item, coupon) and for superior and
// magic items the bonuses it has, ticked at its values.
auto FromItem(const std::string& code, int quality, int uniqueOrSetId, const std::vector<ItemStat>& stats,
              const gamedata::GameData& game) -> RuleDraft;

// The player can finish: an action, a kind, and at least one item.
auto Complete(const RuleDraft& draft) -> bool;
auto PickCount(const RuleDraft& draft) -> std::size_t;

}  // namespace esrfe::ui
