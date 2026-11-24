// A loot filter rule in plain words, for players who never read the file: what it does (hide, always show,
// highlight, rename), which items it is about (names from the mod's data, in the player's language), and when.
//
// Every English text goes through the overlay's UiText, so the translations in locales\<code>\ui-strings.txt
// apply; names of items, types, stats and areas come from the game's strings. No layout here: the views draw it.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "gamedata.h"
#include "json.h"

namespace esrfe::ui {

enum class Action { Hide, Show, Highlight, Rename, Other };

struct RuleSummary {
    Action action = Action::Other;
    std::string actionLabel;              // "Hide", "Always show", "Highlight", "Rename", "Other" (translated)
    std::string what;                     // "Ber Rune, Jah Rune and 3 more", "Helm", "Any item"
    std::vector<std::string> whatAll;     // every item / type / unique name, for the rule's card
    std::vector<std::string> conditions;  // "Superior", "Elite", "+1 to Maximum Curses or more", "Outside towns"
    std::vector<std::string> effects;     // "Shown as: ÿc4Ravenlore", "Plays a sound", "Sends a chat line"
    std::vector<std::int64_t> levels;     // activeInLevels; empty: on every level
    bool hideWhenOthersFail = false;      // hideIfOtherConditionsFail
    std::string preview;                  // the new name with its ÿc colours when the rule renames ("" when not)
    std::string search;                   // everything above plus the codes, one per line, for the search box
};

struct DescribeContext {
    const gamedata::GameData* game = nullptr;  // nullptr: codes are shown as they are
    gamedata::Language language = gamedata::Language::enUS;
    // A style from the filter's "styles" by name, or nullptr (useStyle's actions count for the rule's action).
    std::function<const document::Value*(std::string_view)> style;
    // A filter level's name by id ("" when unknown).
    std::function<std::string(std::int64_t)> levelName;
};

auto Describe(const document::Value& rule, const DescribeContext& context) -> RuleSummary;

// A filter text in the language: a string, or an object of language -> string (enUS, then the first one).
auto PickLanguage(const document::Value& text, gamedata::Language language) -> std::string;

// "1, 2 and 3" style lists, with the translated "and".
auto JoinNames(const std::vector<std::string>& names, std::size_t shown) -> std::string;

}  // namespace esrfe::ui
