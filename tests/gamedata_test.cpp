// Tests for src/gamedata against the real mod files.
//
//   gamedata_test <excel dir> <strings dir>
//
// Defaults to /home/claude/ref/moddata/{excel,strings}.

#include "gamedata.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace gamedata;

namespace {

int Failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            ++Failures;                                                             \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        const auto& va = (a);                                                       \
        const auto& vb = (b);                                                       \
        if (!(va == vb)) {                                                          \
            std::printf("FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);       \
            ++Failures;                                                             \
        }                                                                           \
    } while (0)

auto Has(const std::vector<StatRange>& v, const GameData& d, const char* stat) -> const StatRange* {
    const int id = d.stats.Id(stat);
    for (const auto& s : v)
        if (s.stat == id) return &s;
    return nullptr;
}

auto GroupOf(const GameData& d, const char* code) -> std::string {
    const GroupRef r = d.groups.Of(code);
    return r.group < 0 ? "" : d.groups.All()[static_cast<std::size_t>(r.group)].id;
}

auto SubgroupOf(const GameData& d, const char* code) -> std::string {
    const GroupRef r = d.groups.Of(code);
    if (r.group < 0) return "";
    return d.groups.All()[static_cast<std::size_t>(r.group)].subgroups[static_cast<std::size_t>(r.subgroup)].id;
}

const std::string Yc = "\xC3\xBF" "c";  // "ÿc"

auto TestStrings() -> void {
    CHECK_EQ(Strings::Plain(Yc + "3Flawed " + Yc + ";Soul"), std::string("Flawed Soul"));
    CHECK_EQ(Strings::Plain(Yc + "UBlue"), std::string("Blue"));
    CHECK_EQ(Strings::Plain("[ms]remarquable[fs]remarquable[mp]x"), std::string("remarquable"));
    CHECK_EQ(Strings::Plain("[pl]Helme"), std::string("Helme"));
    CHECK_EQ(Strings::Plain("Level [5]"), std::string("Level [5]"));
    Language l = Language::enUS;
    CHECK(ParseLanguage("ptBR", l) && l == Language::ptBR);
    CHECK(!ParseLanguage("xxXX", l));
    CHECK_EQ(LanguageCode(Language::zhTW), std::string_view("zhTW"));
}

auto TestRowsAndTypes(const GameData& d) -> void {
    // uniqueOrSetIds as the loot filter plugin uses them.
    const std::pair<int, const char*> uniques[] = {
        {12, "Zodiac Band"}, {3, "Deviant Cell"}, {1090, "Lich King Essence Fire"},
        {1098, "Mythical Highlord's Wrath"}, {1099, "Vessel of Souls"}};
    for (const auto& [row, key] : uniques) {
        const Unique* u = d.uniques.At(row);
        CHECK(u != nullptr && u->key == key);
    }
    CHECK(d.uniques.At(1098)->onMythical && d.uniques.At(1099)->onMythical);
    CHECK(!d.uniques.At(12)->onMythical);

    // Item type rows: the "Expansion" separator is not counted.
    CHECK_EQ(d.types.Row("shie"), 2);
    CHECK_EQ(d.types.Row("blun"), 57);
    CHECK_EQ(d.types.Row("jewl"), 58);
    CHECK_EQ(d.types.Row("clas"), 59);
    CHECK(d.types.IsA("xbow", "weap"));
    CHECK(d.types.IsA("h2h2", "assn"));
    CHECK(d.types.IsA("zrin", "ring"));
    CHECK(d.types.IsA("pelt", "helm"));
    CHECK(!d.types.IsA("jave", "miss"));
    CHECK(d.types.IsA("abow", "abow"));
    const auto kids = d.types.Descendants(d.types.Row("char"));
    CHECK(std::find(kids.begin(), kids.end(), d.types.Row("scha")) != kids.end());
    const ItemType* weap = d.types.Find("miss");
    CHECK(weap != nullptr && weap->parents.size() == 1 && weap->parents[0] == d.types.Row("weap"));
    CHECK_EQ(d.types.DisplayName(d.types.Row("tors"), d.strings, Language::deDE), std::string("Rüstung"));
}

auto TestBases(const GameData& d) -> void {
    int mythical = 0, weapons = 0, armor = 0;
    std::set<std::string> codes;
    for (const auto& b : d.bases.All()) {
        if (!b.mythical) continue;
        ++mythical;
        codes.insert(b.code);
        if (b.kind == BaseKind::Weapon) ++weapons;
        if (b.kind == BaseKind::Armor) ++armor;
        CHECK(b.tier == Tier::Elite);
        CHECK_EQ(b.level, 96);
    }
    CHECK_EQ(mythical, 61);
    CHECK_EQ(weapons, 56);
    CHECK_EQ(armor, 5);
    std::set<std::string> expected = {"dd1"};
    for (int i = 1; i <= 61; ++i) {
        if (i == 19) continue;
        char c[8];
        std::snprintf(c, sizeof c, "m%02d", i);
        expected.insert(c);
    }
    CHECK(codes == expected);

    const Base* sword = d.bases.Find("7wd");
    CHECK(sword != nullptr && !sword->mythical && sword->tier == Tier::Elite);
    CHECK_EQ(d.BaseName("7wd", Language::enUS), std::string("Mythical Sword"));
    for (const char* c : {"mam", "mrn", "mjw"}) {
        const Base* b = d.bases.Find(c);
        CHECK(b != nullptr && b->kind == BaseKind::Misc && b->mythicalMisc && !b->mythical && b->level == 100);
    }

    CHECK(d.bases.Find("hax")->tier == Tier::Normal);
    CHECK(d.bases.Find("9ha")->tier == Tier::Exceptional);
    CHECK(d.bases.Find("7ha")->tier == Tier::Elite);
    CHECK(d.bases.Find("rin")->tier == Tier::None);

    // Names in German.
    CHECK_EQ(d.BaseName("lxb", Language::deDE), std::string("Leichte Armbrust"));
    CHECK_EQ(d.BaseName("m01", Language::deDE), std::string("Mythischer Scharfschützenbogen"));
    CHECK_EQ(d.BaseName("xl0", Language::deDE), std::string("Fehlerhafte Gefrorene Seele"));
    CHECK_EQ(d.strings.Text("xl0", Language::deDE), Yc + "3Fehlerhafte Gefrorene Seele");
    CHECK_EQ(d.uniques.Name(*d.uniques.At(12), d.strings, Language::deDE), std::string("Zodiac-Band"));

    // Sockets: base column capped by the type.
    const Base* m01 = d.bases.Find("m01");
    CHECK_EQ(m01->maxSockets, 6);
    CHECK_EQ(d.bases.MaxSocketsAt(*m01, 20, d.types), 3);

    // Rarities.
    const Rarities rune = d.bases.Find("r51")->rarities;
    CHECK(rune.normal && !rune.magic && !rune.rare && !rune.unique && !rune.set && !rune.superior);
    const Rarities charm = d.bases.Find("cm1")->rarities;
    CHECK(!charm.normal && charm.magic && charm.unique && !charm.superior);
    const Rarities ring = d.bases.Find("rin")->rarities;
    CHECK(!ring.normal && ring.magic && ring.rare && ring.unique && ring.set);
    const Rarities xbow = d.bases.Find("lxb")->rarities;
    CHECK(xbow.normal && xbow.superior && xbow.magic && xbow.rare && xbow.unique);
    const Rarities gem = d.bases.Find("gsv")->rarities;
    CHECK(gem.normal && !gem.magic && !gem.rare);
}

auto TestSets(const GameData& d) -> void {
    const SetItem* first = d.sets.Item(0);
    CHECK(first != nullptr && first->key == "Autolycus' Robes");
    const Set* set = d.sets.SetOf(*first);
    CHECK(set != nullptr && set->key == "Autolycus' Magic Tools");
    CHECK(std::find(set->items.begin(), set->items.end(), 0) != set->items.end());
}

auto TestStats(const GameData& d) -> void {
    const int ps = d.stats.Id("projectilespeed");
    CHECK(ps >= 0);
    CHECK_EQ(d.StatLine(ps, 25, 0, Language::enUS), std::string("+25% Projectile Speed"));
    CHECK_EQ(d.StatLine(ps, 25, 0, Language::deDE), std::string("+25% Projektilgeschwindigkeit"));
    CHECK_EQ(d.StatLine(d.stats.Id("strength"), 10, 0, Language::enUS), std::string("+10 to Strength"));
    CHECK_EQ(d.StatLine(d.stats.Id("fireresist"), -20, 0, Language::enUS), std::string("Fire Resist -20%"));
    CHECK_EQ(d.StatLine(d.stats.Id("item_elemskillcold"), 1, 0, Language::enUS), std::string("+1 to Cold Skills"));

    // Skill params.
    const Stat* tab = d.stats.Find("item_addskill_tab");
    CHECK(tab != nullptr && tab->param == ParamKind::SkillTab && tab->SkillParam());
    CHECK_EQ(d.StatLine(tab->id, 2, 2, Language::enUS), std::string("+2 to Javelin and Spear Skills (Amazon Only)"));
    CHECK_EQ(d.ParamChoices(tab->id, Language::enUS).size(), std::size_t{24});
    CHECK_EQ(d.SkillTabName(7 * 8 + 0, Language::enUS), std::string("Demon (Warlock)"));
    const Stat* cls = d.stats.Find("item_addclassskills");
    CHECK(cls != nullptr && cls->param == ParamKind::Class);
    CHECK_EQ(d.StatLine(cls->id, 1, 3, Language::enUS), std::string("+1 to Paladin Skill Levels"));
    CHECK_EQ(d.ParamName(cls->id, 1, Language::enUS), std::string("Sorceress"));

    const Skill* fireBall = d.skills.FindByName("Fire Ball");  // ESR's Frost Nova is not a class skill
    CHECK(fireBall != nullptr && fireBall->classCode == "sor");
    const Stat* hit = d.stats.Find("item_skillonhit");
    CHECK(hit != nullptr && hit->param == ParamKind::SkillLevel);
    CHECK_EQ(d.StatLine(hit->id, 10, (fireBall->id << 6) | 5, Language::enUS),
             std::string("10% Chance to Cast Level 5 Fire Ball on Striking"));
    CHECK_EQ(d.StatLine(d.stats.Id("item_charged_skill"), (20 << 8) | 15, (fireBall->id << 6) | 7, Language::enUS),
             std::string("Level 7 Fire Ball (15/20 Charges)"));
    CHECK_EQ(d.StatLine(d.stats.Id("item_singleskill"), 2, fireBall->id, Language::enUS),
             std::string("+2 to Fire Ball (Sorceress Only)"));
    const Stat* aura = d.stats.Find("item_aura");
    CHECK(aura != nullptr && aura->param == ParamKind::Skill);  // only properties.txt tells
    CHECK(!d.stats.Find("strength")->SkillParam());
}

auto TestSuperior(const GameData& d) -> void {
    const auto xbow = d.SuperiorStatsFor("lxb");
    for (const char* s : {"totalcrossbowbolts", "totalcrossbowdamage", "crossbowreloadspeedlimit", "projectilespeed"})
        CHECK(Has(xbow, d, s) != nullptr);
    const StatRange* ps = Has(xbow, d, "projectilespeed");
    CHECK(ps != nullptr && ps->min == 5 && ps->max == 25);
    CHECK(Has(d.SuperiorStatsFor("cap"), d, "extra_curse") != nullptr);
    CHECK(Has(d.SuperiorStatsFor("jav"), d, "projectilespeed") == nullptr);
    CHECK(Has(d.SuperiorStatsFor("sbw"), d, "projectilespeed") != nullptr);
    CHECK(Has(d.SuperiorStatsFor("rin"), d, "projectilespeed") == nullptr);

    // Over every weapon and armor base: which bases each one rolls on.
    std::vector<std::string> all;
    for (const auto& b : d.bases.All())
        if (b.kind != BaseKind::Misc) all.push_back(b.code);
    const auto every = d.SuperiorStatsFor(all);
    const auto isA = [&](const std::string& code, const char* type) {
        const Base* b = d.bases.Find(code);
        return d.types.IsA(b->typeRow, d.types.Row(type)) || d.types.IsA(b->type2Row, d.types.Row(type));
    };
    const auto rollsOnlyOn = [&](const char* stat, const char* type) {
        const StatRange* s = Has(every, d, stat);
        if (s == nullptr) return false;
        std::size_t typed = 0;
        for (const auto& code : all) typed += isA(code, type) ? 1 : 0;
        for (const auto& code : s->bases)
            if (!isA(code, type)) return false;
        return s->bases.size() == typed;
    };
    CHECK(rollsOnlyOn("projectilespeed", "miss"));
    CHECK(rollsOnlyOn("totalcrossbowbolts", "xbow"));
    CHECK(rollsOnlyOn("crossbowreloadspeedlimit", "xbow"));
    CHECK(rollsOnlyOn("extra_curse", "helm"));
    const StatRange* every_ps = Has(every, d, "projectilespeed");
    bool bows = false, xbows = false, javs = false;
    for (const auto& code : every_ps->bases) {
        bows = bows || isA(code, "bow");
        xbows = xbows || isA(code, "xbow");
        javs = javs || isA(code, "jave");
    }
    CHECK(bows && xbows && !javs);
}

auto TestMagic(const GameData& d) -> void {
    const auto ring = d.MagicStatsFor("rin");
    const StatRange* fcr = Has(ring, d, "item_fastercastrate");
    CHECK(fcr != nullptr && fcr->min > 0 && !fcr->sources.empty() && fcr->minItemLevel > 0);
    CHECK(Has(ring, d, "item_armor_percent") == nullptr);
    CHECK(!d.MagicAffixesFor("rin").empty());
    CHECK(d.MagicStatsFor("r51").empty());

    // A group: rings and amulets together, each stat says where it rolls.
    const auto both = d.MagicStatsFor(std::vector<std::string>{"rin", "amu"});
    const StatRange* fcr2 = Has(both, d, "item_fastercastrate");
    CHECK(fcr2 != nullptr && fcr2->bases.size() == 2);

    // Rare: only affixes with rare = 1, so each stat also rolls on a magic one, never higher.
    const auto rareRing = d.RareStatsFor("rin");
    CHECK(!rareRing.empty() && Has(rareRing, d, "item_fastercastrate") != nullptr);
    for (const char* code : {"rin", "jew"}) {
        const auto magic = d.MagicStatsFor(code);
        const auto rare = d.RareStatsFor(code);
        bool narrower = rare.size() < magic.size();
        for (const StatRange& r : rare) {
            const StatRange* m = nullptr;
            for (const StatRange& x : magic)
                if (x.stat == r.stat && x.param == r.param) m = &x;
            CHECK(m != nullptr && r.max <= m->max);
            if (m != nullptr && r.max < m->max) narrower = true;
        }
        if (std::string(code) == "jew") CHECK(narrower);  // the best jewel affixes (Exarch's, of Carnage...) are magic only
    }
    CHECK(d.RareStatsFor("r51").empty());
}

auto TestGroups(const GameData& d) -> void {
    CHECK_EQ(GroupOf(d, "lxb"), std::string("weapons"));
    CHECK_EQ(SubgroupOf(d, "lxb"), std::string("xbows"));
    CHECK_EQ(GroupOf(d, "xl0"), std::string("crystals"));
    CHECK_EQ(GroupOf(d, "xt0"), std::string("crystals"));
    CHECK_EQ(GroupOf(d, "r51"), std::string("runes"));
    CHECK_EQ(GroupOf(d, "mrn"), std::string("jewelry"));
    CHECK_EQ(GroupOf(d, "zrn"), std::string("jewelry"));
    CHECK_EQ(GroupOf(d, "cm1"), std::string("charms"));
    CHECK_EQ(GroupOf(d, "mjw"), std::string("jewels"));
    CHECK_EQ(GroupOf(d, "gld"), std::string("gold"));
    CHECK_EQ(GroupOf(d, "hpo"), std::string("potions"));
    CHECK_EQ(GroupOf(d, "key"), std::string("quest"));
    CHECK_EQ(SubgroupOf(d, "ob1"), std::string("orbs"));
    CHECK_EQ(SubgroupOf(d, "ktr"), std::string("claws"));
    CHECK_EQ(SubgroupOf(d, "qui"), std::string("body"));
    CHECK_EQ(SubgroupOf(d, "cap"), std::string("helms"));
    CHECK_EQ(SubgroupOf(d, "ba1"), std::string("barbh"));
    CHECK_EQ(SubgroupOf(d, "m06"), std::string("body"));

    // Every base in exactly one group.
    std::map<std::string, int> seen;
    std::size_t total = 0;
    for (const auto& g : d.groups.All())
        for (const auto& s : g.subgroups)
            for (const auto& code : s.bases) {
                ++seen[code];
                ++total;
            }
    CHECK_EQ(total, d.bases.All().size());
    CHECK_EQ(seen.size(), d.bases.All().size());
    for (const auto& b : d.bases.All()) CHECK(seen[b.code] == 1);

    const Group* w = d.groups.Find("weapons");
    CHECK(w != nullptr && Groups::Name(*w, d.strings, Language::deDE) == "Waffen");
}

auto PrintSummary(const GameData& d) -> void {
    for (const auto& g : d.groups.All()) {
        std::printf("  %-8s %-22s %4zu:", g.id.c_str(), Groups::Name(g, d.strings, Language::enUS).c_str(), g.BaseCount());
        for (const auto& s : g.subgroups)
            std::printf(" %s(%zu, %s)", Groups::Name(s, d.strings, Language::enUS).c_str(), s.bases.size(), s.bases[0].c_str());
        std::printf("\n");
    }
}

}  // namespace

auto main(int argc, char** argv) -> int {
    const std::string excel = argc > 1 ? argv[1] : "/home/claude/ref/moddata/excel";
    const std::string strings = argc > 2 ? argv[2] : "/home/claude/ref/moddata/strings";

    TestStrings();

    GameData d;
    std::vector<std::string> problems;
    const bool ok = GameData::Load(excel, strings, Language::enUS, d, problems);
    std::printf("loaded in %.1f ms: %zu bases, %zu uniques, %zu set items, %zu stats, %zu strings\n",
                d.loadMilliseconds, d.bases.All().size(), d.uniques.All().size(), d.sets.Items().size(),
                d.stats.All().size(), d.strings.Count());
    for (const auto& p : problems) std::printf("  data: %s\n", p.c_str());
    CHECK(ok);
    if (!ok) return 1;

    TestRowsAndTypes(d);
    TestBases(d);
    TestSets(d);
    TestStats(d);
    TestSuperior(d);
    TestMagic(d);
    TestGroups(d);
    PrintSummary(d);

    if (Failures > 0) {
        std::printf("%d FAILED\n", Failures);
        return 1;
    }
    std::printf("ALL GAMEDATA TESTS PASSED\n");
    return 0;
}
