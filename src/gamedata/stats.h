// itemstatcost.txt (stats and their tooltip wording), skills and classes.
#pragma once

#include "game_strings.h"
#include "table.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gamedata {

// What a stat's param means.
enum class ParamKind {
    None,
    Class,        // class id 0..7 (item_addclassskills)
    SkillTab,     // class * 8 + tab (item_addskill_tab)
    Skill,        // skill id (item_singleskill, item_aura, oskills)
    SkillLevel,   // skill id << 6 | skill level (skill on hit, charges)
    MonsterType,  // montype.txt row: not in the data we read, shown as a number
    Monster,      // monstats.txt row: same
    Other,        // some other number
};

struct Stat {
    int id = -1;
    std::string name;          // the Stat column ("projectilespeed")
    int descFunc = 0;
    int descVal = 0;           // 0: no value, 1: value first, 2: value last
    int descPriority = 0;
    std::string descPos;       // strings keys
    std::string descNeg;
    std::string desc2;
    int saveParamBits = 0;
    int encode = 0;
    ParamKind param = ParamKind::None;

    auto SkillParam() const -> bool {
        return param == ParamKind::Class || param == ParamKind::SkillTab || param == ParamKind::Skill ||
               param == ParamKind::SkillLevel;
    }
};

struct Skill {
    int id = -1;
    std::string name;      // skills.txt "skill" column
    std::string nameKey;   // skilldesc "str name"
    std::string classCode; // "ama"... or empty
    int classId = -1;
    int tab = -1;          // 0..2 from skilldesc SkillPage, -1 if none
};

// The playable classes. charstats.txt is not read: this is its data.
struct PlayerClass {
    int id;
    std::string_view code;         // "ama"
    std::string_view nameKey;      // "amazo" -> "Amazon"
    std::string_view allSkillsKey; // "%+d to Amazon Skill Levels"
    std::string_view onlyKey;      // "(Amazon Only)"
    std::string_view tabItemKeys[3];  // "%+d to Bow and Crossbow Skills" ...
    std::string_view tabNameKeys[3];  // "Bow and Crossbow" ...
};
auto PlayerClasses() -> const std::vector<PlayerClass>&;
auto FindClass(std::string_view code) -> const PlayerClass*;
auto ClassById(int id) -> const PlayerClass*;

struct SkillTab {
    int param;      // class * 8 + tab, as the stat item_addskill_tab holds it
    int classId;
    int tab;
};

class Stats {
public:
    auto Load(const Table& table, std::vector<std::string>& problems) -> void;

    auto All() const -> const std::vector<Stat>& { return stats_; }
    auto At(int id) const -> const Stat*;
    auto Find(std::string_view name) const -> const Stat*;
    auto Id(std::string_view name) const -> int;

    // Used by Properties to fill in params that only the property funcs tell.
    auto SetParamKind(int id, ParamKind kind) -> void;

private:
    std::vector<Stat> stats_;  // by id
    std::unordered_map<std::string, int> byName_;
};

class Skills {
public:
    auto Load(const Table& skills, const Table& skilldesc, std::vector<std::string>& problems) -> void;

    auto All() const -> const std::vector<Skill>& { return skills_; }
    auto At(int id) const -> const Skill*;
    auto FindByName(std::string_view name) const -> const Skill*;

    // A mod param: a number or a skill name. -1 if neither.
    auto ParamToId(std::string_view param) const -> int;

    auto Name(int id, const Strings& strings, Language language) const -> std::string;

    // Every skill tab (8 classes x 3).
    auto Tabs() const -> std::vector<SkillTab>;

private:
    std::vector<Skill> skills_;  // by id
    std::unordered_map<std::string, int> byName_;
};

}  // namespace gamedata
