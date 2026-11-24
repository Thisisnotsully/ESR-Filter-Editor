// A filter made from scratch by the new-filter wizard (standard C++ only; tested on Linux).
//
// The starter (data/new-filter, made by tools/starter/make_starter.py) is a whole filter and its parts: which rules
// each wizard part is. The player's answers (a Recipe) say which parts are on, the levels and character levels of
// each step, the filter's levels, and the label colours. MakeFilter applies them to the starter filter.
//
// A made filter is a version like any other: its base is
//     <filters folder>\esr-filter-editor\made\<name>\<name>.json        (id "made:<name>.json")
// with its answers next to it (recipe.json) and its sounds and icons; the game loads "user:<name>.json", the base
// with the player's own rules and changes on top. Changing the answers later remakes the base, and the player's
// own changes come through as they do through an author's update.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "document.h"
#include "session.h"

namespace esrfe::edit {

struct StarterStep {
    std::string key;  // name, levels, protect, gold, junk, gear, uniques, announce, look, check
    std::string title;
    std::string help;
};

struct StarterPart {
    enum class Kind { Show, Hide, Effect };
    std::string key;
    std::string step;
    Kind kind = Kind::Hide;
    std::string label;
    std::string help;
    std::vector<std::string> rules;  // the starter's rule ids, in the filter's order
    bool rows = true;                // each rule can be edited (levels, character level, gold, towns)
    bool minimal = false;            // on in the minimal start
};

struct StarterLook {
    std::string style;  // a style of the starter filter ("UniqueLabel")
    std::string label;  // "Uniques"
};

struct Starter {
    std::string folder;      // where it was read from (sounds/ is there)
    std::string from;        // "Sully's QoL Filter 0.9.6"
    std::string filterText;  // starter-filter.json
    document::Document filter;
    std::vector<std::int64_t> towns;  // the town areas a hide step leaves alone
    std::vector<StarterStep> steps;
    std::vector<StarterPart> parts;
    std::vector<StarterLook> looks;

    [[nodiscard]] auto Part(const std::string& key) const -> const StarterPart*;
    [[nodiscard]] auto Levels() const -> int { return static_cast<int>(filter.GetSettings().filterLevels.size()); }
};

auto LoadStarter(const std::string& folder, Starter& out, std::string& error) -> bool;

using Rgb3 = std::array<int, 3>;

// One rule of a part, as the player wants it.
struct Row {
    std::string id;      // the starter's rule ("R084"), or a step the player added ("W001")
    std::string copyOf;  // an added step: the rule it copies
    bool on = true;
    int from = 1;  // filter levels from .. to (1-based); to 0 = the last level
    int to = 0;
    int clvl = 0;            // from this character level (0: any)
    std::int64_t amount = -1;  // gold piles up to this (-1: not a gold step)
    bool towns = false;        // a hide step: also in towns
};

struct PartChoice {
    std::string key;
    bool on = true;
    std::vector<Row> rows;  // empty: the part's rules as they are
};

struct LookChoice {
    std::string style;
    std::optional<Rgb3> border;
    std::optional<Rgb3> background;
    std::optional<Rgb3> beam;
};

struct Recipe {
    std::string name;
    std::string author;
    bool minimal = false;
    std::vector<std::string> levels;  // the filter levels' names (1-5)
    int defaultLevel = 1;
    int soundVolume = 25;
    std::vector<PartChoice> parts;
    std::vector<LookChoice> looks;

    [[nodiscard]] auto Part(const std::string& key) const -> const PartChoice*;
    auto Part(const std::string& key) -> PartChoice*;
    [[nodiscard]] auto Look(const std::string& style) const -> const LookChoice*;
    auto Look(const std::string& style) -> LookChoice&;  // made when missing
    [[nodiscard]] auto NextAddedId() const -> std::string;  // "W001", ...
};

// The starter's own answers (everything on, or only the minimal parts), its levels and rows as the rules are.
auto DefaultRecipe(const Starter& starter, bool minimal) -> Recipe;
// A rule of the starter as a row.
auto RowOf(const Starter& starter, const std::string& id) -> Row;
// The style's colours as the starter has them.
auto StarterLookOf(const Starter& starter, const std::string& style) -> LookChoice;

auto ParseRecipe(std::string_view text, Recipe& out, std::string& error) -> bool;
auto RenderRecipe(const Recipe& recipe) -> std::string;

// The filter the answers make. `icons`: the icon files are there (else icons are left out of the rules).
auto MakeFilter(const Starter& starter, const Recipe& recipe, bool icons, std::string& text, std::string& error) -> bool;

// Makes the filter in the player's folders and switches the game to it: "user:<name>.json" in `mergedId`.
auto CreateFilter(const Folders& folders, const Starter& starter, const Recipe& recipe, std::string& mergedId, std::string& error) -> bool;

}  // namespace esrfe::edit
