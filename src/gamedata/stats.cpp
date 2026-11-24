#include "stats.h"

namespace gamedata {

auto PlayerClasses() -> const std::vector<PlayerClass>& {
    // From the game's charstats.txt (StrAllSkills, StrClassOnly, StrSkillTab1-3)
    // and the skill tree names. Tab 0..2 is skilldesc SkillPage 1..3. The
    // Warlock's tabs come from its skills: page 1 holds the demons, page 3
    // the fire spells.
    static const std::vector<PlayerClass> classes = {
        {0, "ama", "amazo", "ModStr3a", "AmaOnly", {"StrSklTabItem3", "StrSklTabItem2", "StrSklTabItem1"},
         {"SkillCategoryAm3", "SkillCategoryAm2", "SkillCategoryAm1"}},
        {1, "sor", "sorce", "ModStr3d", "SorOnly", {"StrSklTabItem15", "StrSklTabItem14", "StrSklTabItem13"},
         {"SkillCategorySo3", "SkillCategorySo2", "SkillCategorySo1"}},
        {2, "nec", "necro", "ModStr3c", "NecOnly", {"StrSklTabItem8", "StrSklTabItem7", "StrSklTabItem9"},
         {"SkillCategoryNe3", "SkillCategoryNe2", "SkillCategoryNe1"}},
        {3, "pal", "palad", "ModStr3b", "PalOnly", {"StrSklTabItem6", "StrSklTabItem5", "StrSklTabItem4"},
         {"SkillCategoryPa3", "SkillCategoryPa2", "SkillCategoryPa1"}},
        {4, "bar", "barbh", "ModStr3e", "BarOnly", {"StrSklTabItem11", "StrSklTabItem12", "StrSklTabItem10"},
         {"SkillCategoryBa3", "SkillCategoryBa2", "SkillCategoryBa1"}},
        {5, "dru", "druid", "ModStre8a", "DruOnly", {"StrSklTabItem16", "StrSklTabItem17", "StrSklTabItem18"},
         {"SkillCategoryDr3", "SkillCategoryDr2", "SkillCategoryDr1"}},
        {6, "ass", "assas", "ModStre8b", "AssOnly", {"StrSklTabItem19", "StrSklTabItem20", "StrSklTabItem21"},
         {"SkillCategoryAs3", "SkillCategoryAs2", "SkillCategoryAs1"}},
        {7, "war", "warlo", "ModStrge9", "WarOnly", {"StrSklTabItem24", "StrSklTabItem22", "StrSklTabItem23"},
         {"SkillCategoryWa1", "SkillCategoryWa2", "SkillCategoryWa3"}},
    };
    return classes;
}

auto FindClass(std::string_view code) -> const PlayerClass* {
    for (const auto& c : PlayerClasses())
        if (c.code == code) return &c;
    return nullptr;
}

auto ClassById(int id) -> const PlayerClass* {
    const auto& all = PlayerClasses();
    return id >= 0 && id < static_cast<int>(all.size()) ? &all[static_cast<std::size_t>(id)] : nullptr;
}

auto Stats::Load(const Table& t, std::vector<std::string>& problems) -> void {
    stats_.clear();
    byName_.clear();
    const int cStat = t.NeedCol("Stat", problems);
    const int cId = t.Col("*ID");
    const int cFunc = t.Col("descfunc");
    const int cVal = t.Col("descval");
    const int cPrio = t.Col("descpriority");
    const int cPos = t.Col("descstrpos");
    const int cNeg = t.Col("descstrneg");
    const int c2 = t.Col("descstr2");
    const int cBits = t.Col("Save Param Bits");
    const int cEnc = t.Col("Encode");

    for (int r = 0; r < t.Rows(); ++r) {
        Stat s;
        s.id = t.Int(r, cId, r);
        if (s.id != r) problems.push_back("itemstatcost.txt: row " + std::to_string(r) + " has *ID " + std::to_string(s.id));
        s.id = r;  // the game counts rows
        s.name = t.Str(r, cStat);
        s.descFunc = t.Int(r, cFunc);
        s.descVal = t.Int(r, cVal);
        s.descPriority = t.Int(r, cPrio);
        s.descPos = t.Str(r, cPos);
        s.descNeg = t.Str(r, cNeg);
        s.desc2 = t.Str(r, c2);
        s.saveParamBits = t.Int(r, cBits);
        s.encode = t.Int(r, cEnc);
        switch (s.descFunc) {
            case 13: s.param = ParamKind::Class; break;
            case 14: s.param = ParamKind::SkillTab; break;
            case 15: case 24: s.param = ParamKind::SkillLevel; break;
            case 16: case 27: case 28: s.param = ParamKind::Skill; break;
            case 22: s.param = ParamKind::MonsterType; break;
            case 23: s.param = ParamKind::Monster; break;
            default:
                if (s.encode == 1) s.param = ParamKind::Skill;
                else if (s.encode == 2 || s.encode == 3) s.param = ParamKind::SkillLevel;
                else if (s.saveParamBits > 0) s.param = ParamKind::Other;
                break;
        }
        byName_.emplace(s.name, s.id);
        stats_.push_back(std::move(s));
    }
}

auto Stats::At(int id) const -> const Stat* {
    return id >= 0 && id < static_cast<int>(stats_.size()) ? &stats_[static_cast<std::size_t>(id)] : nullptr;
}

auto Stats::Find(std::string_view name) const -> const Stat* { return At(Id(name)); }

auto Stats::Id(std::string_view name) const -> int {
    const auto it = byName_.find(std::string(name));
    return it == byName_.end() ? -1 : it->second;
}

auto Stats::SetParamKind(int id, ParamKind kind) -> void {
    if (id >= 0 && id < static_cast<int>(stats_.size())) stats_[static_cast<std::size_t>(id)].param = kind;
}

auto Skills::Load(const Table& t, const Table& desc, std::vector<std::string>& problems) -> void {
    skills_.clear();
    byName_.clear();
    const int cSkill = t.NeedCol("skill", problems);
    const int cId = t.Col("*Id");
    const int cClass = t.Col("charclass");
    const int cDesc = t.Col("skilldesc");

    std::unordered_map<std::string, int> descRow;
    const int dName = desc.NeedCol("skilldesc", problems);
    const int dStr = desc.Col("str name");
    const int dPage = desc.Col("SkillPage");
    for (int r = 0; r < desc.Rows(); ++r) descRow.emplace(desc.Str(r, dName), r);

    for (int r = 0; r < t.Rows(); ++r) {
        Skill s;
        s.id = r;
        if (t.Int(r, cId, r) != r) problems.push_back("skills.txt: row " + std::to_string(r) + " has another *Id");
        s.name = t.Str(r, cSkill);
        s.classCode = t.Str(r, cClass);
        if (const PlayerClass* c = FindClass(s.classCode)) s.classId = c->id;
        if (const auto it = descRow.find(t.Str(r, cDesc)); it != descRow.end()) {
            s.nameKey = desc.Str(it->second, dStr);
            const int page = desc.Int(it->second, dPage);
            if (page >= 1 && page <= 3 && s.classId >= 0) s.tab = page - 1;
        }
        byName_.emplace(s.name, s.id);
        skills_.push_back(std::move(s));
    }
}

auto Skills::At(int id) const -> const Skill* {
    return id >= 0 && id < static_cast<int>(skills_.size()) ? &skills_[static_cast<std::size_t>(id)] : nullptr;
}

auto Skills::FindByName(std::string_view name) const -> const Skill* {
    const auto it = byName_.find(std::string(name));
    return it == byName_.end() ? nullptr : At(it->second);
}

auto Skills::ParamToId(std::string_view param) const -> int {
    int id = 0;
    if (ParseInt(param, id)) return id;
    const Skill* s = FindByName(param);
    return s != nullptr ? s->id : -1;
}

auto Skills::Name(int id, const Strings& strings, Language language) const -> std::string {
    const Skill* s = At(id);
    if (s == nullptr) return "#" + std::to_string(id);
    if (const std::string* text = strings.Find(s->nameKey, language)) return Strings::Plain(*text);
    return s->name;
}

auto Skills::Tabs() const -> std::vector<SkillTab> {
    std::vector<SkillTab> tabs;
    for (const auto& c : PlayerClasses())
        for (int tab = 0; tab < 3; ++tab) tabs.push_back({c.id * 8 + tab, c.id, tab});
    return tabs;
}

}  // namespace gamedata
