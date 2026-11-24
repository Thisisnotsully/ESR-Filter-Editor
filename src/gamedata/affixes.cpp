#include "affixes.h"

#include <algorithm>

namespace gamedata {

auto Properties::Load(const Table& t, Stats& stats, std::vector<std::string>& problems) -> void {
    props_.clear();
    byCode_.clear();
    const int cCode = t.NeedCol("code", problems);
    int cFunc[7], cStat[7], cSet[7], cVal[7];
    for (int i = 0; i < 7; ++i) {
        const std::string n = std::to_string(i + 1);
        cFunc[i] = t.Col("func" + n);
        cStat[i] = t.Col("stat" + n);
        cSet[i] = t.Col("set" + n);
        cVal[i] = t.Col("val" + n);
    }
    int unknownStats = 0;
    std::string firstUnknown;
    for (int r = 0; r < t.Rows(); ++r) {
        Property p;
        p.code = t.Str(r, cCode);
        if (p.code.empty()) continue;
        for (int i = 0; i < 7; ++i) {
            PropertyFunc f;
            f.func = t.Int(r, cFunc[i]);
            if (f.func == 0) continue;
            const std::string_view statName = t.Cell(r, cStat[i]);
            if (!statName.empty()) {
                f.stat = stats.Id(statName);
                if (f.stat < 0) {
                    if (unknownStats++ == 0) firstUnknown = p.code + " -> " + std::string(statName);
                }
            }
            f.set = t.Int(r, cSet[i]);
            int v = 0;
            f.hasVal = ParseInt(t.Cell(r, cVal[i]), v);
            f.val = v;
            // Params the descfunc doesn't tell.
            if (const Stat* s = stats.At(f.stat); s != nullptr && (s->param == ParamKind::None || s->param == ParamKind::Other)) {
                if (f.func == 22) stats.SetParamKind(f.stat, ParamKind::Skill);
                else if (f.func == 11 || f.func == 19) stats.SetParamKind(f.stat, ParamKind::SkillLevel);
                else if (f.func == 10) stats.SetParamKind(f.stat, ParamKind::SkillTab);
            }
            p.funcs.push_back(f);
        }
        if (byCode_.emplace(p.code, static_cast<int>(props_.size())).second) props_.push_back(std::move(p));
    }
    // ESR keeps vanilla properties whose stats it removed (the /time ones):
    // harmless unless an affix uses them.
    if (unknownStats > 0)
        problems.push_back("properties.txt: " + std::to_string(unknownStats) + " funcs name stats that don't exist, e.g. " + firstUnknown);
    minDamage_ = stats.Id("mindamage");
    maxDamage_ = stats.Id("maxdamage");
    minPercent_ = stats.Id("item_mindamage_percent");
    maxPercent_ = stats.Id("item_maxdamage_percent");
}

auto Properties::Find(std::string_view code) const -> const Property* {
    const auto it = byCode_.find(std::string(code));
    return it == byCode_.end() ? nullptr : &props_[static_cast<std::size_t>(it->second)];
}

auto Properties::Expand(std::string_view code, std::string_view param, int min, int max, const Skills& skills,
                        std::vector<StatRoll>& out) const -> bool {
    const Property* p = Find(code);
    if (p == nullptr) return false;
    if (min > max) std::swap(min, max);
    int numParam = 0;
    const bool paramIsNumber = ParseInt(param, numParam);
    StatRoll last{};
    bool haveLast = false;
    for (const auto& f : p->funcs) {
        StatRoll roll{f.stat, 0, min, max};
        switch (f.func) {
            case 1: case 2: case 8: case 13: case 14: case 20:
                break;
            case 3:
                if (!haveLast) continue;
                roll = {f.stat, last.param, last.min, last.max};
                break;
            case 5: roll.stat = minDamage_; break;
            case 6: roll.stat = maxDamage_; break;
            case 7:
                out.push_back({minPercent_, 0, min, max});
                roll.stat = maxPercent_;
                break;
            case 10: case 21:
                roll.param = f.hasVal ? f.val : (paramIsNumber ? numParam : 0);
                if (f.func == 10) roll.param = (roll.param % 3) + 8 * (roll.param / 3);
                break;
            case 22: case 24: {
                // A number, else a skill name (aura, oskill); states and
                // monster types are numbers here.
                const int id = paramIsNumber ? numParam : skills.ParamToId(param);
                roll.param = id < 0 ? 0 : id;
                break;
            }
            case 11: case 19: {
                const int skill = skills.ParamToId(param);
                roll.param = skill < 0 ? -1 : (skill << 6) | (max & 63);
                // Charges are stored as max << 8 | current, full when found.
                roll.min = roll.max = f.func == 19 ? ((min & 255) << 8) | (min & 255) : min;
                break;
            }
            case 12: case 36:
                roll.param = -1;
                break;
            case 15: roll.max = min; break;
            case 16: roll.min = max; break;
            case 17: case 18:
                roll.min = roll.max = paramIsNumber ? numParam : 0;
                break;
            default:  // 23 ethereal, 25 random affix, others
                continue;
        }
        if (roll.stat < 0) continue;
        out.push_back(roll);
        last = roll;
        haveLast = true;
    }
    return true;
}

namespace {

auto ReadTypes(const Table& t, int row, const std::vector<int>& cols, const ItemTypes& types,
               std::vector<std::string>& problems) -> std::vector<int> {
    std::vector<int> out;
    for (const int c : cols) {
        const std::string_view code = t.Cell(row, c);
        if (code.empty()) continue;
        const int r = types.Row(code);
        if (r < 0) {
            problems.push_back(t.Name() + ": row " + std::to_string(row) + " has unknown item type '" + std::string(code) + "'");
            continue;
        }
        out.push_back(r);
    }
    return out;
}

auto Cols(const Table& t, std::string_view prefix, int count) -> std::vector<int> {
    std::vector<int> cols;
    for (int i = 1; i <= count; ++i) {
        const int c = t.Col(std::string(prefix) + std::to_string(i));
        if (c >= 0) cols.push_back(c);
    }
    return cols;
}

}  // namespace

auto LoadAffixes(const Table& t, AffixKind kind, const ItemTypes& types, std::vector<Affix>& out,
                 std::vector<std::string>& problems) -> void {
    out.clear();
    const int cName = t.NeedCol("Name", problems);
    const int cSpawn = t.Col("spawnable");
    const int cRare = t.Col("rare");
    const int cLevel = t.Col("level");
    const int cMax = t.Col("maxlevel");
    const int cReq = t.Col("levelreq");
    const int cCs = t.Col("classspecific");
    const int cClass = t.Col("class");
    const int cClassReq = t.Col("classlevelreq");
    const int cFreq = t.Col("frequency");
    const int cGroup = t.Col("group");
    const auto itypeCols = Cols(t, "itype", 7);
    const auto etypeCols = Cols(t, "etype", 5);
    int cMod[3][4];
    for (int m = 0; m < 3; ++m) {
        const std::string n = "mod" + std::to_string(m + 1);
        cMod[m][0] = t.Col(n + "code");
        cMod[m][1] = t.Col(n + "param");
        cMod[m][2] = t.Col(n + "min");
        cMod[m][3] = t.Col(n + "max");
    }
    out.reserve(static_cast<std::size_t>(t.Rows()));
    for (int r = 0; r < t.Rows(); ++r) {
        Affix a;
        a.kind = kind;
        a.row = r;
        a.name = t.Str(r, cName);
        a.spawnable = t.Int(r, cSpawn) == 1;
        a.rare = t.Int(r, cRare) == 1;
        a.level = t.Int(r, cLevel);
        a.maxLevel = t.Int(r, cMax);
        a.levelReq = t.Int(r, cReq);
        a.classSpecific = t.Str(r, cCs);
        a.classCode = t.Str(r, cClass);
        a.classLevelReq = t.Int(r, cClassReq);
        a.frequency = t.Int(r, cFreq);
        a.group = t.Int(r, cGroup);
        for (int m = 0; m < 3; ++m) {
            AffixMod mod{t.Str(r, cMod[m][0]), t.Str(r, cMod[m][1]), t.Int(r, cMod[m][2]), t.Int(r, cMod[m][3])};
            if (!mod.code.empty()) a.mods.push_back(std::move(mod));
        }
        a.itypes = ReadTypes(t, r, itypeCols, types, problems);
        a.etypes = ReadTypes(t, r, etypeCols, types, problems);
        out.push_back(std::move(a));
    }
}

auto LoadSuperior(const Table& t, const ItemTypes& types, std::vector<SuperiorMod>& out,
                  std::vector<std::string>& problems) -> void {
    out.clear();
    const auto itypeCols = Cols(t, "itype", 7);
    const auto etypeCols = Cols(t, "etype", 5);
    const int cFreq = t.Col("frequency");
    // The old columns and the item types they mean.
    const std::pair<const char*, const char*> flags[] = {
        {"armor", "armo"}, {"weapon", "weap"}, {"shield", "shld"}, {"scepter", "scep"}, {"wand", "wand"},
        {"staff", "staf"}, {"bow", "bow"}, {"boots", "boot"}, {"gloves", "glov"}, {"belt", "belt"},
    };
    int cMod[2][4];
    for (int m = 0; m < 2; ++m) {
        const std::string n = "mod" + std::to_string(m + 1);
        cMod[m][0] = t.Col(n + "code");
        cMod[m][1] = t.Col(n + "param");
        cMod[m][2] = t.Col(n + "min");
        cMod[m][3] = t.Col(n + "max");
    }
    for (int r = 0; r < t.Rows(); ++r) {
        SuperiorMod s;
        s.row = r;
        s.frequency = t.Int(r, cFreq, 1);
        for (int m = 0; m < 2; ++m) {
            AffixMod mod{t.Str(r, cMod[m][0]), t.Str(r, cMod[m][1]), t.Int(r, cMod[m][2]), t.Int(r, cMod[m][3])};
            if (!mod.code.empty()) s.mods.push_back(std::move(mod));
        }
        s.itypes = ReadTypes(t, r, itypeCols, types, problems);
        s.etypes = ReadTypes(t, r, etypeCols, types, problems);
        if (s.itypes.empty()) {
            for (const auto& [col, code] : flags)
                if (t.Int(r, t.Col(col)) == 1) s.itypes.push_back(types.Row(code));
        }
        out.push_back(std::move(s));
    }
}

auto TypesMatch(const ItemTypes& types, int typeRow, int type2Row, const std::vector<int>& itypes,
                const std::vector<int>& etypes) -> bool {
    const auto is = [&](int ancestor) { return types.IsA(typeRow, ancestor) || types.IsA(type2Row, ancestor); };
    bool any = false;
    for (const int t : itypes)
        if (is(t)) { any = true; break; }
    if (!any) return false;
    for (const int t : etypes)
        if (is(t)) return false;
    return true;
}

}  // namespace gamedata
