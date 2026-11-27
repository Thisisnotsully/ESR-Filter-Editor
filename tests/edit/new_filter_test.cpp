// The new-filter wizard's filter maker, on the real starter (data/new-filter).
//   new_filter_test <data/new-filter> [<out folder>]   (out: the made filters, for tools/filtercheck)
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

#include "changes.h"
#include "document.h"
#include "new_filter.h"
#include "session.h"

using namespace esrfe::edit;
using document::Value;

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                \
    do {                                                           \
        ++checks;                                                  \
        if (!(cond)) {                                             \
            ++failures;                                            \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);    \
        }                                                          \
    } while (0)

static auto Read(const std::string& text, document::Document& out) -> bool {
    std::vector<document::Diag> diags;
    return document::Document::Parse(text, out, diags);
}
static auto RuleOf(const document::Document& doc, const std::string& id) -> const Value* {
    const int i = doc.FindRule(id);
    return i < 0 ? nullptr : &doc.Rules()[static_cast<std::size_t>(i)].value;
}
static auto Levels(const Value* rule) -> std::vector<std::int64_t> {
    std::vector<std::int64_t> out;
    const Value* a = rule == nullptr ? nullptr : rule->Find("activeInLevels");
    if (a != nullptr)
        for (const Value& v : a->array) {
            std::int64_t n = 0;
            v.AsInteger(n);
            out.push_back(n);
        }
    return out;
}
static auto HasIcon(const Value& v) -> bool {
    if (!v.IsObject()) return false;
    if (v.Find("icon") != nullptr) return true;
    for (const document::Member& m : v.object)
        if (HasIcon(m.value)) return true;
    return false;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string outFolder = argc > 2 ? argv[2] : "";
    Starter starter;
    std::string error;
    CHECK(LoadStarter(argv[1], starter, error));
    if (!error.empty()) std::printf("  %s\n", error.c_str());
    if (starter.parts.empty()) return 1;
    CHECK(starter.Levels() == 4 && starter.steps.size() == 10 && starter.looks.size() > 10);
    // Every rule in exactly one part.
    {
        std::set<std::string> seen;
        std::size_t total = 0;
        for (const StarterPart& p : starter.parts)
            for (const std::string& id : p.rules) {
                ++total;
                seen.insert(id);
            }
        CHECK(total == seen.size() && static_cast<int>(seen.size()) == starter.filter.ActiveCount());
    }
    const auto write = [&outFolder](const char* name, const std::string& text) {
        if (outFolder.empty()) return;
        std::filesystem::create_directories(outFolder);
        std::ofstream(std::filesystem::path(outFolder) / name, std::ios::binary) << text;
    };

    // Recommended, as it comes: the starter's rules exactly.
    Recipe recommended = DefaultRecipe(starter, false);
    recommended.name = "My filter";
    recommended.author = "Tester";
    CHECK(recommended.levels.size() == 4 && recommended.levels[0] == "Leveling" && recommended.defaultLevel == 3 && recommended.soundVolume == 25);
    std::string text;
    CHECK(MakeFilter(starter, recommended, true, text, error));
    document::Document made;
    CHECK(Read(text, made));
    CHECK(made.ActiveCount() == starter.filter.ActiveCount());
    int same = 0;
    for (const document::Rule& r : starter.filter.Rules()) {
        const Value* m = RuleOf(made, r.id);
        // The same, or the same with "levels 1-4" written as every level, or one level as a list.
        Value a = m == nullptr ? Value::Null() : *m, b = r.value;
        for (Value* v : {&a, &b})
            if (const Value* l = v->Find("activeInLevels"); l != nullptr && l->IsNumber()) v->Set("activeInLevels", Value::Array({*l}));
        if (Levels(&b) == (std::vector<std::int64_t>{1, 2, 3, 4})) b.Erase("activeInLevels");
        // A hide in the starter that also hid in towns: the new filter's leaves them alone.
        if (b.Find("hide") != nullptr && b.Find("excludeAreas") == nullptr && a.Find("excludeAreas") != nullptr) a.Erase("excludeAreas");
        same += m != nullptr && a == b ? 1 : 0;
        if (m == nullptr || !(a == b)) std::printf("  DIFF %s\n", r.id.c_str());
    }
    CHECK(same == starter.filter.ActiveCount());
    CHECK(made.GetSettings().title == "My filter" && made.GetSettings().author == "Tester" && made.GetSettings().updateUrl.empty());
    CHECK(made.GetSettings().filterLevels.size() == 4 && made.GetSettings().filterLevels[1].name.IsObject());  // translations kept
    CHECK(text.rfind("// My filter: made with", 0) == 0 && text.find("0.9.4 - grouped") == std::string::npos);
    write("recommended.json", text);

    // Minimal: the parts not in it are gone.
    Recipe minimal = DefaultRecipe(starter, true);
    minimal.name = "Small";
    CHECK(MakeFilter(starter, minimal, true, text, error));
    document::Document small;
    CHECK(Read(text, small));
    CHECK(small.ActiveCount() < made.ActiveCount());
    CHECK(RuleOf(small, "R001") != nullptr && RuleOf(small, "R070") != nullptr && RuleOf(small, "R105") == nullptr && RuleOf(small, "R119") == nullptr);

    // Three levels, renamed; a gold step changed; a potion step added; uniques' border; no icons.
    Recipe mine = DefaultRecipe(starter, false);
    mine.name = "Three levels";
    mine.levels = {"Easy", "Strict - Nightmare", "Hard"};
    mine.defaultLevel = 5;
    PartChoice* gold = mine.Part("gold");
    CHECK(gold != nullptr);
    for (Row& r : gold->rows)
        if (r.id == "R075") {
            CHECK(r.from == 2 && r.to == 0 && r.clvl == 40 && r.amount == 249);
            r.from = 3;
            r.clvl = 50;
            r.amount = 300;
        }
    PartChoice* minor = mine.Part("minorPotions");
    CHECK(minor != nullptr && minor->rows.size() == 1 && minor->rows[0].clvl == 12);
    Row added = minor->rows[0];
    added.id = mine.NextAddedId();
    added.copyOf = "R084";
    added.from = 2;
    added.clvl = 5;
    minor->rows.push_back(added);
    CHECK(added.id == "W001");
    mine.Part("ethereal")->rows[0].on = false;  // R003 off
    mine.Look("UniqueLabel").border = Rgb3{10, 20, 30};
    mine.Look("SetLabel").beam = Rgb3{1, 2, 3};
    CHECK(MakeFilter(starter, mine, false, text, error));
    document::Document three;
    CHECK(Read(text, three));
    CHECK(three.GetSettings().filterLevels.size() == 3 && three.GetSettings().defaultLevel == 3);
    CHECK(three.GetSettings().filterLevels[0].name.IsString() && three.GetSettings().filterLevels[1].name.IsObject());
    CHECK(RuleOf(three, "R092") == nullptr);                                   // level 4 only: gone
    CHECK(RuleOf(three, "R005") != nullptr && Levels(RuleOf(three, "R005")).empty());  // levels 1-4: now every level
    CHECK(Levels(RuleOf(three, "R004")) == (std::vector<std::int64_t>{1, 2}));
    CHECK(RuleOf(three, "R003") == nullptr);
    const Value* r075 = RuleOf(three, "R075");
    CHECK(r075 != nullptr && Levels(r075) == (std::vector<std::int64_t>{3}) && r075->Find("excludeAreas") != nullptr);  // hides always leave towns alone
    {
        std::int64_t clvl = 0, amount = 0;
        r075->Find("playerStats")->array[0].Find("value")->AsInteger(clvl);
        r075->Find("itemStats")->array[0].Find("value")->AsInteger(amount);
        CHECK(clvl == 50 && amount == 300);
    }
    const int w = three.FindRule("W001");
    CHECK(w == three.FindRule("R084") + 1);
    CHECK(w >= 0 && Levels(RuleOf(three, "W001")) == (std::vector<std::int64_t>{2, 3}) && RuleOf(three, "W001")->Find("excludeAreas") != nullptr);
    bool icons = false;
    for (const document::Rule& r : three.Rules()) icons = icons || HasIcon(r.value);
    for (const document::Style& s : three.Styles()) icons = icons || HasIcon(s.value);
    CHECK(!icons);
    for (const document::Style& s : three.Styles()) {
        if (s.name == "UniqueLabel") CHECK(*s.value.Find("labelBorder")->Find("color") == Value::Array({Value::Integer(10), Value::Integer(20), Value::Integer(30), Value::Integer(255)}));
        if (s.name == "SetLabel") CHECK(*s.value.Find("droplight")->Find("color") == Value::Array({Value::Integer(1), Value::Integer(2), Value::Integer(3)}));
    }
    CHECK(StarterLookOf(starter, "UniqueLabel").border == (Rgb3{255, 165, 0}));
    write("three-levels.json", text);

    // The answers, written and read back.
    Recipe back;
    CHECK(ParseRecipe(RenderRecipe(mine), back, error));
    CHECK(back.name == mine.name && back.levels == mine.levels && back.parts.size() == mine.parts.size());
    CHECK(back.Part("minorPotions")->rows.size() == 2 && back.Part("minorPotions")->rows[1].copyOf == "R084");
    const Recipe& cback = back;
    CHECK(cback.Look("UniqueLabel") != nullptr && cback.Look("UniqueLabel")->border == (Rgb3{10, 20, 30}));
    std::string again;
    CHECK(MakeFilter(starter, back, false, again, error) && again == text);

    // Made in the player's folders: the game switched to it, the sounds next to it.
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "esrfe-new-filter-test";
        fs::remove_all(root);
        const fs::path user = root / "LootFilters";
        fs::create_directories(user);
        Folders folders{user.string(), (root / "shipped").string()};
        std::string merged;
        Recipe r = DefaultRecipe(starter, false);
        r.name = "bad/name";
        CHECK(!CreateFilter(folders, starter, r, merged, error));
        r.name = "My first filter";
        CHECK(CreateFilter(folders, starter, r, merged, error));
        if (!error.empty()) std::printf("  %s\n", error.c_str());
        CHECK(merged == "user:My first filter.json");
        std::string t;
        CHECK(ReadText((user / "My first filter.json").string(), t));
        document::Document game;
        CHECK(Read(t, game) && game.GetSettings().title == "My first filter" && game.ActiveCount() == starter.filter.ActiveCount());
        CHECK(fs::exists(user / "sounds" / "unique.mp3"));
        CHECK(fs::exists(user / "esr-filter-editor" / "made" / "My first filter" / "recipe.json"));
        CHECK(ReadText((user / "selection.json").string(), t) && t.find("\"activeFilter\": \"user:My first filter.json\"") != std::string::npos);
        Session s;
        CHECK(s.Open(merged, (user / "My first filter.json").string(), folders, error) && s.IsCopy() && !s.NeedsName());
        CHECK(s.BaseId() == "made:My first filter.json");
        CHECK(!CreateFilter(folders, starter, r, merged, error));  // the name is taken
        fs::remove_all(root);
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
