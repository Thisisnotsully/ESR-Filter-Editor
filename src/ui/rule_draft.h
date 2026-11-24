// A player's new rule while they build it, step by step: what happens, which items, which tiers (and Mythicals),
// which rarities, which stats, on which filter levels. What each step can offer follows from the steps before (only
// the rarities the chosen bases come in; only the superior or magic stats they can roll). ToRule writes the loot
// filter rule; FromRule reads one back (to change one of the player's own rules); FromItem starts from an item the
// player pointed at, its stats ticked.
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

enum class DraftAction { None, Hide, Show, Highlight };
enum class MythicalMode { Include, Leave, Only };

// The colours a highlight can have (border and beam).
struct HighlightColor {
    const char* english;  // "Red"
    int r, g, b;
};
auto HighlightColors() -> const std::vector<HighlightColor>&;

struct StatPick {
    int stat = -1;
    int param = 0;
    int atLeast = 0;
};

struct RuleDraft {
    DraftAction action = DraftAction::None;
    int color = 0;          // HighlightColors index
    bool beam = false;      // a light beam where it drops
    bool chat = false;      // a chat line when it drops
    std::vector<std::string> bases;  // base codes
    std::set<gamedata::Tier> tiers;  // empty: every tier
    MythicalMode mythical = MythicalMode::Include;
    std::set<int> rarities;          // loot filter quality numbers (2 normal, 3 superior, 4 magic, ...); empty: every one
    std::vector<StatPick> stats;     // only with exactly superior or exactly magic
    std::vector<std::int64_t> levels;  // empty: every level
};

// A stat the chosen items can roll, for the stats step.
struct StatOption {
    int stat = -1;
    int param = 0;
    int min = 0;  // what "at least" can be (above 0)
    int max = 0;
    std::string label;  // "+1 to Maximum Curses" (at the highest roll), plain text
};

struct DraftOffer {
    bool tiers = false;      // some bases have a tier
    std::set<gamedata::Tier> tierChoices;
    bool mythicals = false;  // some bases are Mythical
    std::set<int> rarities;  // the qualities the bases come in
    bool statsPossible = false;  // exactly superior or exactly magic chosen
    std::string statsWhy;        // English, when not: what to choose for stats
    std::vector<StatOption> stats;
};

// The rarities a player can pick, in order (loot filter quality numbers) and their English names.
auto RarityChoices() -> const std::vector<std::pair<int, const char*>>&;

// The bases the draft is about after its tier and Mythical choices.
auto ChosenBases(const RuleDraft& draft, const gamedata::GameData& game) -> std::vector<std::string>;
auto Offer(const RuleDraft& draft, const gamedata::GameData& game) -> DraftOffer;
// Drops choices the items no longer allow (a rarity they do not come in, a stat they cannot roll).
void Tidy(RuleDraft& draft, const gamedata::GameData& game);

// The loot filter rule and its place: "top" for hide and always show, "end" (with keepShown) for highlights. The
// title is "<summary> | <action>" (the session puts the U### id in front).
auto ToRule(const RuleDraft& draft, const gamedata::GameData& game, const std::string& summary) -> edit::OwnRule;
// A rule made by ToRule, back to its draft. False for a rule it cannot have made.
auto FromRule(const document::Value& rule, const gamedata::GameData& game, RuleDraft& out) -> bool;

// The item the player points at: its base, tier and rarity chosen, and for superior and magic items the stats it has
// that the step offers, ticked at its own values.
struct ItemStat {
    int stat = -1;
    int param = 0;
    int value = 0;
};
auto FromItem(const std::string& code, int quality, const std::vector<ItemStat>& stats, const gamedata::GameData& game) -> RuleDraft;

// The player can finish: an action and at least one base.
auto Complete(const RuleDraft& draft) -> bool;

}  // namespace esrfe::ui
