// Everything the editor knows about the mod's items, read from the mod's own
// data files: <mod>.mpq/data/global/excel and data/local/lng/strings.
#pragma once

#include "affixes.h"
#include "groups.h"
#include "item_types.h"
#include "items.h"
#include "stats.h"
#include "game_strings.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gamedata {

// A stat that can roll, merged over every affix (or superior row) that gives
// it with the same param.
struct StatRange {
    int stat = -1;
    int param = 0;
    int min = 0;
    int max = 0;
    int minItemLevel = 0;               // lowest affix level that gives it (0 for superior)
    std::vector<std::string> sources;   // affix name keys, or superior property codes
    std::vector<std::string> bases;     // the bases it rolls on
};

// A choice for a stat's param, for pickers.
struct ParamChoice {
    int param;
    std::string name;
};

struct GameData {
    Language language = Language::enUS;
    Strings strings;
    ItemTypes types;
    Bases bases;
    Uniques uniques;
    Sets sets;
    Stats stats;
    Skills skills;
    Properties properties;
    std::vector<Affix> prefixes;
    std::vector<Affix> suffixes;
    std::vector<Affix> automagic;
    std::vector<SuperiorMod> superior;
    Groups groups;
    std::vector<std::string> areaKeys;  // levels.txt LevelName by area id ("" where none); optional
    double loadMilliseconds = 0;

    // Loads it all. Problems (missing files, unknown codes) go to problems;
    // false if something the editor can't do without is missing.
    static auto Load(const std::string& excelDir, const std::string& stringsDir, Language language,
                     GameData& out, std::vector<std::string>& problems) -> bool;

    // Plain text of a strings key in the current language ("" if unknown).
    auto Text(std::string_view key) const -> std::string;

    // Names in a language (plain: no color codes).
    auto BaseName(std::string_view code, Language lang) const -> std::string;
    auto ClassName(int classId, Language lang) const -> std::string;
    // An area's name ("Rogue Encampment"); "" when unknown.
    auto AreaName(int areaId, Language lang) const -> std::string;
    auto SkillTabName(int param, Language lang) const -> std::string;  // class * 8 + tab

    // Magic affixes that can roll on the base: spawnable, frequency > 0, the
    // base is one of itype and none of etype (type or type2, with ancestors),
    // and classspecific matches the base's class where both have one.
    auto MagicAffixesFor(std::string_view baseCode) const -> std::vector<const Affix*>;

    // Every stat a magic item of the base(s) can roll, merged per (stat, param).
    auto MagicStatsFor(std::string_view baseCode) const -> std::vector<StatRange>;
    auto MagicStatsFor(const std::vector<std::string>& baseCodes) const -> std::vector<StatRange>;

    // Automagic (the base's "auto prefix" group).
    auto AutoMagicStatsFor(std::string_view baseCode) const -> std::vector<StatRange>;

    // Superior bonuses from qualityitems.txt.
    auto SuperiorStatsFor(std::string_view baseCode) const -> std::vector<StatRange>;
    auto SuperiorStatsFor(const std::vector<std::string>& baseCodes) const -> std::vector<StatRange>;

    // A stat's tooltip line for a value and param, raw (with color codes).
    // Per-level stats get the stored value as it is.
    auto StatLine(int stat, int value, int param, Language lang) const -> std::string;

    // What a param means ("Frost Nova", "Amazon", "Bow and Crossbow").
    auto ParamName(int stat, int param, Language lang) const -> std::string;

    // The params a stat can take with their names; empty for stats without
    // a skill/class/tab param. SkillLevel params are listed with level 0.
    auto ParamChoices(int stat, Language lang) const -> std::vector<ParamChoice>;

private:
    enum class Source { Magic, AutoMagic, Superior };
    auto Collect(Source source, const std::vector<std::string>& codes) const -> std::vector<StatRange>;
    auto Text(std::string_view key, Language lang) const -> const std::string&;
    auto StatLineUntrimmed(int stat, int value, int param, Language lang) const -> std::string;
};

}  // namespace gamedata
