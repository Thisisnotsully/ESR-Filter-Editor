#include "gamedata.h"

#include <algorithm>
#include <chrono>
#include <map>

namespace gamedata {

namespace {

auto ClassOfBase(const ItemTypes& types, const Base& b) -> std::string {
    for (const int row : {b.typeRow, b.type2Row})
        for (const int a : types.Ancestors(row))
            if (!types.At(a)->classCode.empty()) return types.At(a)->classCode;
    return {};
}

auto AffixFits(const ItemTypes& types, const Base& b, const std::string& baseClass, const Affix& a) -> bool {
    if (!a.CanRoll()) return false;
    if (!a.classSpecific.empty() && !baseClass.empty() && a.classSpecific != baseClass) return false;
    return TypesMatch(types, b.typeRow, b.type2Row, a.itypes, a.etypes);
}

// Works out what qualities each base can have:
//   normal:   unless its item type is always magic (Magic = 1: rings, charms...)
//   magic:    not always normal (Normal = 1: runes, gems, potions), the type
//             is weapon/armor, always magic or can be rare, and at least one
//             magic affix can roll on it
//   rare:     the type's Rare column, not always normal, and at least one
//             affix with rare = 1 can roll on it
//   superior: weapons and armor that can be plain, with a qualityitems row
//   unique, set: a row in uniqueitems.txt / setitems.txt names the base
//             (not disabled; also rows that only the cube makes)
auto WorkOutRarities(GameData& d) -> void {
    std::map<std::pair<int, int>, std::pair<bool, bool>> affixCache;  // (type, type2) -> (magic, rare)
    std::vector<char> hasUnique(d.bases.All().size(), 0), hasSet(d.bases.All().size(), 0);
    const auto indexOf = [&](const std::string& code) -> int {
        const Base* b = d.bases.Find(code);
        return b == nullptr ? -1 : static_cast<int>(b - d.bases.All().data());
    };
    for (const auto& u : d.uniques.All())
        if (!u.disabled)
            if (const int i = indexOf(u.code); i >= 0) hasUnique[static_cast<std::size_t>(i)] = 1;
    for (const auto& s : d.sets.Items())
        if (!s.disabled)
            if (const int i = indexOf(s.code); i >= 0) hasSet[static_cast<std::size_t>(i)] = 1;

    auto& all = d.bases.Mutable();
    for (std::size_t i = 0; i < all.size(); ++i) {
        Base& b = all[i];
        const ItemType* ty = d.types.At(b.typeRow);
        const bool equipment = b.kind != BaseKind::Misc;
        const bool alwaysMagic = ty != nullptr && ty->alwaysMagic;
        const bool alwaysNormal = ty != nullptr && ty->alwaysNormal;
        const bool rareFlag = ty != nullptr && ty->canBeRare;

        const auto key = std::make_pair(b.typeRow, b.type2Row);
        auto it = affixCache.find(key);
        if (it == affixCache.end()) {
            const std::string cls = ClassOfBase(d.types, b);
            bool magic = false, rare = false;
            for (const auto* list : {&d.prefixes, &d.suffixes}) {
                for (const auto& a : *list) {
                    if (!AffixFits(d.types, b, cls, a)) continue;
                    magic = true;
                    rare = rare || a.rare;
                }
            }
            it = affixCache.emplace(key, std::make_pair(magic, rare)).first;
        }
        bool superior = false;
        for (const auto& s : d.superior)
            if (s.frequency > 0 && TypesMatch(d.types, b.typeRow, b.type2Row, s.itypes, s.etypes)) { superior = true; break; }

        Rarities& r = b.rarities;
        r.normal = !alwaysMagic;
        r.magic = !alwaysNormal && (equipment || alwaysMagic || rareFlag) && it->second.first;
        r.rare = !alwaysNormal && rareFlag && it->second.second;
        r.superior = equipment && !alwaysMagic && !alwaysNormal && superior;
        r.unique = hasUnique[i] != 0;
        r.set = hasSet[i] != 0;
    }
}

// printf for the game's strings: %d %i %u %s with flags (+), %% and the
// positional %0..%9 that a few D2R strings use.
struct Arg {
    bool text = false;
    int number = 0;
    std::string str;
};
auto Num(int n) -> Arg { return {false, n, {}}; }
auto Str(std::string s) -> Arg { return {true, 0, std::move(s)}; }

auto ArgText(const Arg& a, bool plus) -> std::string {
    if (a.text) return a.str;
    std::string s = std::to_string(a.number);
    if (plus && a.number >= 0) s.insert(s.begin(), '+');
    return s;
}

auto HasFormat(std::string_view s) -> bool {
    for (std::size_t i = 0; i + 1 < s.size(); ++i) {
        if (s[i] != '%') continue;
        const char c = s[i + 1];
        if (c == '%') { ++i; continue; }
        if (c == '+' || c == 'd' || c == 'i' || c == 'u' || c == 's' || (c >= '0' && c <= '9')) return true;
    }
    return false;
}

auto Format(std::string_view fmt, const std::vector<Arg>& args) -> std::string {
    std::string out;
    std::size_t next = 0;
    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%' || i + 1 >= fmt.size()) { out += fmt[i]; continue; }
        std::size_t j = i + 1;
        if (fmt[j] == '%') { out += '%'; i = j; continue; }
        bool plus = false;
        while (j < fmt.size() && (fmt[j] == '+' || fmt[j] == '-' || fmt[j] == ' ' || fmt[j] == '#')) {
            plus = plus || fmt[j] == '+';
            ++j;
        }
        std::size_t digitsStart = j;
        while (j < fmt.size() && fmt[j] >= '0' && fmt[j] <= '9') ++j;
        const bool hasDigits = j > digitsStart;
        if (j < fmt.size() && (fmt[j] == 'd' || fmt[j] == 'i' || fmt[j] == 'u' || fmt[j] == 's')) {
            if (next < args.size()) out += ArgText(args[next], plus);
            ++next;
            i = j;
        } else if (hasDigits && !plus) {
            const std::size_t index = static_cast<std::size_t>(fmt[digitsStart] - '0');
            if (index < args.size()) out += ArgText(args[index], false);
            i = digitsStart;  // one digit only
        } else {
            out += fmt[i];
        }
    }
    return out;
}

}  // namespace

auto GameData::AreaName(int areaId, Language lang) const -> std::string {
    if (areaId < 0 || static_cast<std::size_t>(areaId) >= areaKeys.size()) return {};
    const std::string& key = areaKeys[static_cast<std::size_t>(areaId)];
    if (key.empty()) return {};
    const std::string* text = strings.Find(key, lang);
    return text != nullptr && !text->empty() ? Strings::Plain(*text) : key;
}

auto GameData::Load(const std::string& excelDir, const std::string& stringsDir, Language language,
                    GameData& d, std::vector<std::string>& problems) -> bool {
    const auto start = std::chrono::steady_clock::now();
    d = GameData{};
    d.language = language;
    bool ok = true;
    const auto load = [&](Table& t, const char* name) {
        if (!t.Load(excelDir + "/" + name, problems)) ok = false;
    };
    Table itemtypes, itemstatcost, properties, skills, skilldesc, weapons, armor, misc, uniqueitems, setitems,
        sets, magicprefix, magicsuffix, automagic, qualityitems;
    load(itemtypes, "itemtypes.txt");
    load(itemstatcost, "itemstatcost.txt");
    load(properties, "properties.txt");
    load(skills, "skills.txt");
    load(skilldesc, "skilldesc.txt");
    load(weapons, "weapons.txt");
    load(armor, "armor.txt");
    load(misc, "misc.txt");
    load(uniqueitems, "uniqueitems.txt");
    load(setitems, "setitems.txt");
    load(sets, "sets.txt");
    load(magicprefix, "magicprefix.txt");
    load(magicsuffix, "magicsuffix.txt");
    load(automagic, "automagic.txt");
    load(qualityitems, "qualityitems.txt");
    if (!d.strings.LoadFolder(stringsDir, problems)) ok = false;
    {
        // Areas only name the filter's area conditions, so a missing levels.txt is not an error.
        Table levels;
        std::vector<std::string> ignored;
        if (levels.Load(excelDir + "/levels.txt", ignored)) {
            const int id = levels.Col("Id");
            const int name = levels.Col("LevelName");
            for (int r = 0; id >= 0 && name >= 0 && r < levels.Rows(); ++r) {
                const int area = levels.Int(r, id, -1);
                if (area < 0 || area > 4096) continue;
                if (static_cast<std::size_t>(area) >= d.areaKeys.size()) d.areaKeys.resize(static_cast<std::size_t>(area) + 1);
                d.areaKeys[static_cast<std::size_t>(area)] = levels.Str(r, name);
            }
        }
    }

    d.types.Load(itemtypes, problems);
    d.stats.Load(itemstatcost, problems);
    d.skills.Load(skills, skilldesc, problems);
    d.properties.Load(properties, d.stats, problems);
    d.bases.Load(weapons, armor, misc, d.types, problems);
    d.uniques.Load(uniqueitems, d.bases, problems);
    d.sets.Load(setitems, sets, d.bases, problems);
    LoadAffixes(magicprefix, AffixKind::Prefix, d.types, d.prefixes, problems);
    LoadAffixes(magicsuffix, AffixKind::Suffix, d.types, d.suffixes, problems);
    LoadAffixes(automagic, AffixKind::AutoMagic, d.types, d.automagic, problems);
    LoadSuperior(qualityitems, d.types, d.superior, problems);
    WorkOutRarities(d);
    d.groups.Build(d.types, d.bases);

    // Mods whose property is unknown.
    for (const auto* list : {&d.prefixes, &d.suffixes, &d.automagic})
        for (const auto& a : *list)
            for (const auto& m : a.mods)
                if (d.properties.Find(m.code) == nullptr)
                    problems.push_back("affix '" + a.name + "' uses unknown property '" + m.code + "'");
    int missingNames = 0;
    for (const auto& b : d.bases.All())
        if (!d.strings.Has(b.nameKey)) ++missingNames;
    if (missingNames > 0)
        problems.push_back(std::to_string(missingNames) + " base names are not in the strings (name column used)");

    if (d.types.All().empty() || d.bases.All().empty() || d.stats.All().empty()) ok = false;
    d.loadMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return ok;
}

auto GameData::Text(std::string_view key, Language lang) const -> const std::string& {
    return strings.Text(key, lang);
}

auto GameData::Text(std::string_view key) const -> std::string { return Strings::Plain(strings.Text(key, language)); }

auto GameData::BaseName(std::string_view code, Language lang) const -> std::string {
    const Base* b = bases.Find(code);
    return b == nullptr ? std::string(code) : bases.Name(*b, strings, lang);
}

auto GameData::ClassName(int classId, Language lang) const -> std::string {
    const PlayerClass* c = ClassById(classId);
    if (c == nullptr) return "#" + std::to_string(classId);
    return Strings::Plain(Text(c->nameKey, lang));
}

auto GameData::SkillTabName(int param, Language lang) const -> std::string {
    const PlayerClass* c = ClassById(param >> 3);
    const int tab = param & 7;
    if (c == nullptr || tab > 2) return "#" + std::to_string(param);
    return Strings::Plain(Text(c->tabNameKeys[tab], lang)) + " (" + ClassName(c->id, lang) + ")";
}

auto GameData::MagicAffixesFor(std::string_view baseCode) const -> std::vector<const Affix*> {
    std::vector<const Affix*> out;
    const Base* b = bases.Find(baseCode);
    if (b == nullptr) return out;
    const std::string cls = ClassOfBase(types, *b);
    for (const auto* list : {&prefixes, &suffixes})
        for (const auto& a : *list)
            if (AffixFits(types, *b, cls, a)) out.push_back(&a);
    return out;
}

auto GameData::Collect(Source source, const std::vector<std::string>& codes) const -> std::vector<StatRange> {
    std::map<std::pair<int, int>, StatRange> acc;
    std::vector<StatRoll> rolls;
    const auto add = [&](const std::string& base, const std::string& sourceName, int level) {
        for (const auto& r : rolls) {
            auto [it, fresh] = acc.try_emplace({r.stat, r.param});
            StatRange& s = it->second;
            if (fresh) {
                s.stat = r.stat;
                s.param = r.param;
                s.min = r.min;
                s.max = r.max;
                s.minItemLevel = level;
            } else {
                s.min = std::min(s.min, r.min);
                s.max = std::max(s.max, r.max);
                s.minItemLevel = std::min(s.minItemLevel, level);
            }
            if (std::find(s.sources.begin(), s.sources.end(), sourceName) == s.sources.end()) s.sources.push_back(sourceName);
            if (std::find(s.bases.begin(), s.bases.end(), base) == s.bases.end()) s.bases.push_back(base);
        }
    };
    for (const auto& code : codes) {
        const Base* b = bases.Find(code);
        if (b == nullptr) continue;
        if (source == Source::Superior) {
            for (const auto& s : superior) {
                if (s.frequency <= 0 || !TypesMatch(types, b->typeRow, b->type2Row, s.itypes, s.etypes)) continue;
                for (const auto& m : s.mods) {
                    rolls.clear();
                    properties.Expand(m.code, m.param, m.min, m.max, skills, rolls);
                    add(b->code, m.code, 0);
                }
            }
            continue;
        }
        const std::string cls = ClassOfBase(types, *b);
        if (source == Source::AutoMagic && b->autoPrefix == 0) continue;
        const auto& lists = source == Source::Magic ? std::vector<const std::vector<Affix>*>{&prefixes, &suffixes}
                                                    : std::vector<const std::vector<Affix>*>{&automagic};
        for (const auto* list : lists) {
            for (const auto& a : *list) {
                if (source == Source::AutoMagic && a.group != b->autoPrefix) continue;
                if (!AffixFits(types, *b, cls, a)) continue;
                rolls.clear();
                for (const auto& m : a.mods) properties.Expand(m.code, m.param, m.min, m.max, skills, rolls);
                add(b->code, a.name, a.level);
            }
        }
    }
    std::vector<StatRange> out;
    out.reserve(acc.size());
    for (auto& [key, s] : acc) out.push_back(std::move(s));
    // As the game lists them: higher descpriority first.
    std::stable_sort(out.begin(), out.end(), [&](const StatRange& a, const StatRange& b) {
        const Stat* sa = stats.At(a.stat);
        const Stat* sb = stats.At(b.stat);
        const int pa = sa ? sa->descPriority : 0, pb = sb ? sb->descPriority : 0;
        return pa > pb;
    });
    return out;
}

auto GameData::MagicStatsFor(std::string_view baseCode) const -> std::vector<StatRange> {
    return Collect(Source::Magic, {std::string(baseCode)});
}
auto GameData::MagicStatsFor(const std::vector<std::string>& codes) const -> std::vector<StatRange> {
    return Collect(Source::Magic, codes);
}
auto GameData::AutoMagicStatsFor(std::string_view baseCode) const -> std::vector<StatRange> {
    return Collect(Source::AutoMagic, {std::string(baseCode)});
}
auto GameData::SuperiorStatsFor(std::string_view baseCode) const -> std::vector<StatRange> {
    return Collect(Source::Superior, {std::string(baseCode)});
}
auto GameData::SuperiorStatsFor(const std::vector<std::string>& codes) const -> std::vector<StatRange> {
    return Collect(Source::Superior, codes);
}

auto GameData::ParamName(int statId, int param, Language lang) const -> std::string {
    const Stat* s = stats.At(statId);
    if (s == nullptr) return std::to_string(param);
    switch (s->param) {
        case ParamKind::Class: return ClassName(param, lang);
        case ParamKind::SkillTab: return SkillTabName(param, lang);
        case ParamKind::Skill: return skills.Name(param, strings, lang);
        case ParamKind::SkillLevel: return skills.Name(param >> 6, strings, lang);
        default: return std::to_string(param);
    }
}

auto GameData::ParamChoices(int statId, Language lang) const -> std::vector<ParamChoice> {
    std::vector<ParamChoice> out;
    const Stat* s = stats.At(statId);
    if (s == nullptr) return out;
    switch (s->param) {
        case ParamKind::Class:
            for (const auto& c : PlayerClasses()) out.push_back({c.id, ClassName(c.id, lang)});
            break;
        case ParamKind::SkillTab:
            for (const auto& t : skills.Tabs()) out.push_back({t.param, SkillTabName(t.param, lang)});
            break;
        case ParamKind::Skill:
        case ParamKind::SkillLevel:
            for (const auto& sk : skills.All()) {
                if (sk.nameKey.empty() || !strings.Has(sk.nameKey)) continue;
                const int p = s->param == ParamKind::Skill ? sk.id : sk.id << 6;
                out.push_back({p, skills.Name(sk.id, strings, lang)});
            }
            break;
        default: break;
    }
    return out;
}

auto GameData::StatLine(int statId, int value, int param, Language lang) const -> std::string {
    std::string line = StatLineUntrimmed(statId, value, param, lang);
    while (!line.empty() && line.back() == ' ') line.pop_back();  // an empty "(Class Only)"
    return line;
}

auto GameData::StatLineUntrimmed(int statId, int value, int param, Language lang) const -> std::string {
    const Stat* s = stats.At(statId);
    if (s == nullptr) return "#" + std::to_string(statId) + " " + std::to_string(value);
    const std::string& key = value < 0 && !s->descNeg.empty() ? s->descNeg : s->descPos;
    std::string str = Text(key, lang);
    const std::string str2 = s->desc2.empty() ? std::string{} : Text(s->desc2, lang);
    const auto skillName = [&](int id) { return skills.Name(id, strings, lang); };
    // Two stats itemstatcost.txt gives no text: the game words them itself.
    if (str.empty() && s->descFunc == 0) {
        if (s->name == "item_nonclassskill") return Format(Text("ItemModifierNonClassSkill", lang), {Num(value), Str(skillName(param))});
        if (s->name == "item_aura") return Format(Text("ModitemAura", lang), {Num(value), Str(skillName(param))});
        return {};  // a stat the game never shows
    }
    const auto onlyText = [&](int classId) -> std::string {
        const PlayerClass* c = ClassById(classId);
        return c == nullptr ? std::string{} : Text(c->onlyKey, lang);
    };
    const auto appendWord = [](std::string a, const std::string& b) {
        if (b.empty()) return a;
        if (a.empty()) return b;
        return a + " " + b;
    };

    int v = value;
    switch (s->descFunc) {
        case 5: case 10: v = value * 100 / 128; break;
        case 20: case 21: v = -value; break;
        default: break;
    }

    switch (s->descFunc) {
        case 11: {  // repairs 1 durability in 100 / value seconds
            const int seconds = value > 0 ? 100 / value : 0;
            return HasFormat(str) ? Format(str, {Num(1), Num(seconds)}) : str;
        }
        case 13: {  // class skills: the string is the class's
            const PlayerClass* c = ClassById(param);
            if (c != nullptr) str = Text(c->allSkillsKey, lang);
            return HasFormat(str) ? Format(str, {Num(v)}) : appendWord(ArgText(Num(v), true), str);
        }
        case 14: {  // skill tab: param = class * 8 + tab
            const PlayerClass* c = ClassById(param >> 3);
            const int tab = param & 7;
            if (c != nullptr && tab <= 2) str = Text(c->tabItemKeys[tab], lang);
            std::string line = HasFormat(str) ? Format(str, {Num(v)}) : appendWord(ArgText(Num(v), true), str);
            return c != nullptr ? appendWord(line, onlyText(c->id)) : line;
        }
        case 15:  // chance to cast: value = chance, param = skill << 6 | level
            return Format(str, {Num(v), Num(param & 63), Str(skillName(param >> 6))});
        case 16:  // aura: value = level, param = skill
            return Format(str, {Num(v), Str(skillName(param))});
        case 22:  // versus monster type: montype.txt is not read
            return appendWord(HasFormat(str) ? Format(str, {Num(v)}) : str, "#" + std::to_string(param));
        case 23:  // %0 value, %1 monster: monstats.txt is not read
            return Format(str, {Num(v), Str("#" + std::to_string(param))});
        case 24:  // charges: param = skill << 6 | level, value = max << 8 | charges
            return Format(str, {Num(param & 63), Str(skillName(param >> 6)), Num(value & 255), Num((value >> 8) & 255)});
        case 27: {  // +n to skill (class only)
            const Skill* sk = skills.At(param);
            return Format(str, {Num(v), Str(skillName(param)), Str(sk ? onlyText(sk->classId) : std::string{})});
        }
        case 28:
            return Format(str, {Num(v), Str(skillName(param))});
        default:
            break;
    }

    std::string line;
    if (HasFormat(str)) {
        line = Format(str, {Num(v)});
    } else if (s->descFunc == 12 && value <= 1) {
        line = str;  // "Freezes Target", "+n" only above 1
    } else {
        std::string number;
        switch (s->descFunc) {
            case 1: case 6: case 12: number = ArgText(Num(v), true); break;
            case 2: case 5: case 7: case 10: case 20: number = std::to_string(v) + "%"; break;
            case 4: case 8: number = ArgText(Num(v), true) + "%"; break;
            default: number = std::to_string(v); break;
        }
        if (s->descVal == 1) line = appendWord(number, str);
        else if (s->descVal == 2) line = appendWord(str, number);
        else line = str;
    }
    switch (s->descFunc) {
        case 6: case 7: case 8: case 9: case 10: case 19:
            line = appendWord(line, str2);
            break;
        default: break;
    }
    return line;
}

}  // namespace gamedata
