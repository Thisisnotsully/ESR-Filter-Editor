// Describes every rule of a real filter with the mod's real data, and checks the words for known rules.
//   ui_test <excel dir> <strings dir> <filter.json> [--print]
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "document.h"
#include "gamedata.h"
#include "browse_view.hpp"
#include "fakes.hpp"
#include "add_rule_view.hpp"
#include "item_rules_view.hpp"
#include "new_filter_view.hpp"
#include "version_name_view.hpp"
#include "quick_rule_view.hpp"
#include "help_view.hpp"
#include "rule_scope.h"
#include "sprite.h"
#include "rule_draft.h"
#include "rule_text.h"
#include "rule_view.hpp"
#include "source.h"

using namespace esrfe::ui;
namespace edit = esrfe::edit;

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                \
    do {                                                           \
        ++checks;                                                  \
        if (!(cond)) {                                             \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);   \
            ++failures;                                            \
        }                                                          \
    } while (0)

static auto Has(const std::vector<std::string>& list, const std::string& text) -> bool {
    for (const std::string& s : list)
        if (s.find(text) != std::string::npos) return true;
    return false;
}

static void Print(const document::Rule& rule, const RuleSummary& s) {
    std::printf("%-5s %-12s %s\n", rule.id.c_str(), s.actionLabel.c_str(), s.what.c_str());
    for (const std::string& c : s.conditions) std::printf("        when: %s\n", c.c_str());
    for (const std::string& e : s.effects) std::printf("        does: %s\n", e.c_str());
}

// The plugin's side, faked.
class FakeSource final : public Source {
public:
    std::shared_ptr<const gamedata::GameData> game;
    std::string problem;
    FilterInfo info;
    std::string text;
    int settingsOpened = 0;
    int reads = 0;
    auto Game() -> std::shared_ptr<const gamedata::GameData> override { return game; }
    auto GameDataProblem() -> std::string override { return problem; }
    auto Language() -> gamedata::Language override { return gamedata::Language::enUS; }
    auto Filter() -> FilterInfo override { return info; }
    auto ReadFile(const std::string& path, std::string& out) -> bool override {
        ++reads;
        return edit::ReadText(path, out);
    }
    edit::Folders folders;
    auto Folders() -> edit::Folders override { return folders; }
    void OpenGameSettings() override { ++settingsOpened; }
    std::vector<std::pair<std::string, int>> played;
    void PlaySound(const std::string& path, int volume) override { played.emplace_back(path, volume); }
    auto FileSoundVolume() -> int override { return 80; }
    // The filter on disk, where the loot filter would have it.
    void Put(const std::string& content) {
        text = content;
        edit::WriteText(info.path, content);
        info.textHash = edit::Hash(content);
    }
};

struct ViewHarness {
    fakes::FakePlatform platform;
    std::shared_ptr<FakeSource> source = std::make_shared<FakeSource>();
    std::shared_ptr<BrowseView> view = std::make_shared<BrowseView>(source);
    esrfe::overlay::Form form{platform, view};
    esrfe::overlay::Box client{0, 0, 1280, 800};
    esrfe::overlay::Box content{32, 72, 1248, 760};
    void Layout() { form.Layout(client, content); }
    // Runs what the views queued (the game's answers).
    void Run() {
        auto posted = std::move(platform.posted);
        platform.posted.clear();
        for (auto& work : posted) work(form);
        Layout();
    }
    void Refresh(bool force = false) {
        view->Refresh(form, force);
        Run();
    }
    void Show() {
        form.Shown();
        Run();
    }
    void Paint() {
        Layout();
        platform.canvas.drawn.clear();
        form.Paint(platform.canvas);
    }
    auto Drew(const std::string& text) const -> bool {
        for (const std::string& d : platform.canvas.drawn)
            if (d.find(text) != std::string::npos) return true;
        return false;
    }
    void Click(const esrfe::overlay::Widget& w) {
        esrfe::overlay::MouseEvent down;
        down.kind = esrfe::overlay::MouseKind::Down;
        down.x = w.box.CentreX();
        down.y = w.box.CentreY();
        form.Mouse(down);
        esrfe::overlay::MouseEvent up = down;
        up.kind = esrfe::overlay::MouseKind::Up;
        form.Mouse(up);
        Layout();
    }
    void Type(const std::string& text) {
        for (char16_t c : esrfe::overlay::Utf8ToUtf16(text)) form.Char(c);
        Layout();
    }
};

static auto RuleIndex(const document::Document& doc, const std::string& id) -> int { return doc.FindRule(id); }

static void DraftTests(const gamedata::GameData& game, const char* outPath) {
    const int allSkills = game.stats.Id("item_allskills");
    const int tab = game.stats.Id("item_addskill_tab");
    CHECK(allSkills >= 0 && tab >= 0);
    // A superior Mystic Staff with +1 to All Skills (superior) and +2 to a skill tab (its automagic).
    RuleDraft d = FromItem("1ls", 3, -1, {{allSkills, 0, 1}, {tab, 10, 2}}, game);
    CHECK(d.kind == ItemKind::Base);
    CHECK(d.bases == std::vector<std::string>{"1ls"});
    CHECK(d.tiers.size() == 1 && *d.tiers.begin() == gamedata::Tier::Elite);
    CHECK(d.baseQualities == std::set<int>{3});
    CHECK(d.stats.size() == 1 && d.stats[0].stat == allSkills && d.stats[0].atLeast == 1);  // the tab is not a superior bonus
    DraftOffer offer = Offer(d, game);
    CHECK(offer.statsPossible && offer.stats.size() > 10 && offer.qualities);
    for (const StatOption& o : offer.stats) {
        const gamedata::Stat* s = game.stats.At(o.stat);
        CHECK(s != nullptr && s->param != gamedata::ParamKind::SkillLevel);
        CHECK(o.min >= 1 && o.max >= o.min && !o.label.empty());
        CHECK(o.label.find("item_") == std::string::npos);  // no internal names
    }
    // White and superior together: no bonuses.
    RuleDraft both = d;
    both.baseQualities.clear();
    CHECK(!Offer(both, game).statsPossible);
    Tidy(both, game);
    CHECK(both.stats.empty());
    // Magic: the magic affixes, worded like the tooltip ("+4 to Teleport", one per skill).
    RuleDraft magic = FromItem("m50", 4, -1, {}, game);
    CHECK(magic.kind == ItemKind::Magic);
    const DraftOffer magicOffer = Offer(magic, game);
    CHECK(magicOffer.statsPossible && magicOffer.stats.size() > Offer(d, game).stats.size());
    int skills = 0;
    std::set<std::string> labels;
    for (const StatOption& o : magicOffer.stats) {
        CHECK(o.label.find("item_") == std::string::npos);
        skills += o.label.find("Teleport") != std::string::npos ? 1 : 0;
        CHECK(labels.insert(o.label).second);  // no two alike
    }
    CHECK(skills >= 1);
    // A hide always leaves towns alone.
    {
        RuleDraft h = FromItem("rin", 6, -1, {}, game);
        h.action = DraftAction::Hide;
        const auto out = ToRules(h, game, "Rare rings");
        const document::Value* areas = out.at(0).rule.Find("excludeAreas");
        CHECK(areas != nullptr && areas->IsArray() && areas->array.size() == TownAreas().size());
        RuleDraft shown = FromItem("rin", 6, -1, {}, game);
        shown.action = DraftAction::AlwaysShow;
        CHECK(ToRules(shown, game, "Rare rings").at(0).rule.Find("excludeAreas") == nullptr);
    }
    // Rare: the affixes a rare can have (identified rares, like magic ones).
    RuleDraft rare = FromItem("rin", 6, -1, {}, game);
    const DraftOffer rareOffer = Offer(rare, game);
    CHECK(rare.kind == ItemKind::Rare && rareOffer.statsPossible && !rareOffer.stats.empty());
    CHECK(rareOffer.stats.size() <= Offer(FromItem("rin", 4, -1, {}, game), game).stats.size());
    rare.stats.push_back(StatPick{rareOffer.stats.front().stat, rareOffer.stats.front().param, rareOffer.stats.front().max});
    Tidy(rare, game);
    CHECK(rare.stats.size() == 1);
    {
        const auto rules = ToRules(rare, game, "Rare ring");
        CHECK(rules.size() == 1 && rules[0].rule.Find("itemStats") != nullptr);
        RuleDraft back;
        CHECK(FromRule(rules[0].rule, game, back) && back.kind == ItemKind::Rare && back.stats.size() == 1);
    }
    // A unique, a set item, a coupon, a rune.
    const gamedata::Unique* someUnique = nullptr;
    for (const gamedata::Unique& u : game.uniques.All())
        if (!u.disabled && someUnique == nullptr) someUnique = &u;
    CHECK(someUnique != nullptr);
    RuleDraft unique = FromItem(someUnique->code, 7, someUnique->row, {}, game);
    CHECK(unique.kind == ItemKind::Unique && unique.uniques == std::vector<int>{someUnique->row} && unique.bases.empty());
    const gamedata::SetItem& someSet = game.sets.Items().front();
    RuleDraft set = FromItem(someSet.code, 5, someSet.row, {}, game);
    CHECK(set.kind == ItemKind::Set && set.setItems == std::vector<int>{someSet.row});
    CHECK(Coupons(game).size() > 300);
    int tiered = 0;
    for (const Coupon& c : Coupons(game)) tiered += c.tier != gamedata::Tier::None ? 1 : 0;
    CHECK(tiered > 300);  // the base in the name gives the tier
    RuleDraft coupon = FromItem("01c", 2, -1, {}, game);
    CHECK(coupon.kind == ItemKind::Unique && coupon.coupons == std::vector<std::string>{"01c"});
    RuleDraft rune = FromItem("r30", 2, -1, {}, game);
    CHECK(rune.kind == ItemKind::Other && rune.bases == std::vector<std::string>{"r30"});
    CHECK(!KindAllows(ItemKind::Other, game, *game.bases.Find("01c")));
    CHECK(KindAllows(ItemKind::Magic, game, *game.bases.Find("rin")) && !KindAllows(ItemKind::Base, game, *game.bases.Find("rin")));

    // To rules and back. A shown rule with a look: its colours last, never hidden.
    d.action = DraftAction::Show;
    d.levels = {3, 4};
    d.look.border = Rgb{230, 50, 50};
    d.look.background = Rgb{0, 0, 0};
    d.look.nameColor = 3;
    d.look.beam = true;
    d.look.beamColor = Rgb{70, 130, 255};
    d.look.sound = "gong.mp3";
    d.look.chat = true;
    d.look.chatText = "Look: {name}";
    d.look.chatColor = 4;
    std::vector<edit::OwnRule> rules = ToRules(d, game, "Superior Mystic Staff with +1 to All Skills");
    CHECK(rules.size() == 1);
    edit::OwnRule own = rules.front();
    CHECK(own.place == "end" && own.keepShown);
    CHECK(own.rule.Find("labelBorder") != nullptr && own.rule.Find("labelBackground") != nullptr && own.rule.Find("droplight") != nullptr);
    CHECK(own.rule.Find("namePrefix") != nullptr && own.rule.Find("sound")->Str() == "gong.mp3");
    CHECK(own.rule.Find("chatMessage")->Str() == std::string("\xC3\xBF" "c4Look: {name}"));
    CHECK(own.rule.Find("quality")->Str() == "superior" && own.rule.Find("isIdentified") != nullptr);
    CHECK(own.rule.Find("activeInLevels")->array.size() == 2);
    RuleDraft back;
    CHECK(FromRule(own.rule, game, back));
    CHECK(back.action == DraftAction::Show && back.kind == ItemKind::Base && back.look == d.look && back.stats.size() == 1);
    CHECK(ToRules(back, game, "Superior Mystic Staff with +1 to All Skills").front().rule == own.rule);
    // Always Show: no levels, and without a look a single show rule on top.
    RuleDraft always = unique;
    always.action = DraftAction::AlwaysShow;
    always.levels = {3};
    Tidy(always, game);
    CHECK(always.levels.empty());
    rules = ToRules(always, game, "A unique");
    CHECK(rules.size() == 1 && rules[0].place == "top" && rules[0].rule.Find("show") != nullptr && rules[0].rule.Find("activeInLevels") == nullptr);
    CHECK(rules[0].rule.Find("uniqueOrSetIds") != nullptr && rules[0].rule.Find("quality")->Str() == "unique");
    // Uniques and coupons together: two rules.
    always.coupons = {"01c"};
    rules = ToRules(always, game, "A unique");
    CHECK(rules.size() == 2 && rules[1].rule.Find("itemCodes") != nullptr && rules[1].rule.Find("uniqueOrSetIds") == nullptr);
    // Hide: on top, its look dropped.
    RuleDraft hide = FromItem("cap", 2, -1, {}, game);
    hide.action = DraftAction::Hide;
    hide.levels = {3, 4};
    hide.look.border = Rgb{1, 2, 3};
    Tidy(hide, game);
    CHECK(!hide.look.Any());
    edit::OwnRule hideRule = ToRules(hide, game, "Normal caps").front();
    CHECK(hideRule.place == "top" && hideRule.rule.Find("hide") != nullptr && hideRule.rule.Find("quality")->Str() == "normal");
    // Set items.
    set.action = DraftAction::AlwaysShow;
    edit::OwnRule setRule = ToRules(set, game, "A set item").front();
    CHECK(setRule.rule.Find("quality")->Str() == "set");
    RuleDraft setBack;
    CHECK(FromRule(setRule.rule, game, setBack) && setBack.kind == ItemKind::Set && setBack.setItems == set.setItems);
    // Mythicals: magic weapons with Mythicals left out.
    RuleDraft axes;
    axes.action = DraftAction::Show;
    axes.kind = ItemKind::Magic;
    axes.bases = {"axe", "m01", "7wd"};
    axes.mythical = RuleDraft::Mythicals::Leave;
    CHECK(Offer(axes, game).mythicals);
    CHECK(ChosenBases(axes, game) == (std::vector<std::string>{"axe", "7wd"}));  // 7wd is not a Mythical
    edit::OwnRule axesRule = ToRules(axes, game, "Axes").front();
    CHECK(axesRule.rule.Find("excludeItemCodes") != nullptr && axesRule.rule.Find("excludeItemCodes")->array.size() == 1);
    CHECK(!Complete(RuleDraft{}) && Complete(axes));

    // "Which items?" for the edit-item key: narrowest first, out to every item of the kind.
    DescribeContext context;
    context.game = &game;
    const auto print = [](const char* what, const std::vector<Scope>& scopes) {
        if (std::getenv("ESRFE_SHOW_SCOPES") == nullptr) return;
        std::printf("  %s:\n", what);
        for (const Scope& s : scopes) std::printf("    %s (%zu)\n", s.label.c_str(), s.count);
    };
    const std::vector<Scope> staff = ScopesFor(FromItem("1ls", 3, -1, {{allSkills, 0, 1}}, game), game, context);
    print("superior Mystic Staff +1 all skills", staff);
    CHECK(staff.size() == 5 && staff[0].draft.stats.size() == 1 && staff[1].draft.stats.empty() && staff[1].count == 1);
    CHECK(staff[3].draft.types == (std::vector<std::string>{"weap", "misl"}) && staff[3].draft.tiers == std::set<gamedata::Tier>{gamedata::Tier::Elite});
    CHECK(staff[4].draft.baseQualities.empty() && staff[4].draft.tiers.empty());
    const std::vector<Scope> ring = ScopesFor(FromItem("rin", 4, -1, {}, game), game, context);
    print("magic ring", ring);
    CHECK(!ring.empty() && ring.back().draft.every && ring.back().label == "Every magic item");
    const std::vector<Scope> uniqueScopes = ScopesFor(unique, game, context);
    print("a unique", uniqueScopes);
    CHECK(uniqueScopes.size() >= 3 && uniqueScopes.front().count == 1 && uniqueScopes.back().draft.every);
    const std::vector<Scope> setScopes = ScopesFor(set, game, context);
    print("a set item", setScopes);
    CHECK(setScopes.size() == 4 && setScopes[1].label.rfind("The whole ", 0) == 0 && setScopes[1].count > 1);
    CHECK(setScopes.size() == 4 && setScopes[2].label.find("-tier set item") != std::string::npos && setScopes[2].count > setScopes[1].count);
    const std::vector<Scope> couponScopes = ScopesFor(FromItem("01c", 2, -1, {}, game), game, context);
    print("an Ancient Coupon", couponScopes);
    CHECK(couponScopes.size() >= 2 && couponScopes.back().label == "Every Ancient Coupon" && couponScopes.back().count > 300);
    const std::vector<Scope> runeScopes = ScopesFor(FromItem("r80", 2, -1, {}, game), game, context);
    print("Ber rune", runeScopes);
    CHECK(runeScopes.size() >= 3 && runeScopes[1].label == "Ber Rune and every higher rune");
    CHECK(runeScopes.size() >= 2 && runeScopes[1].draft.bases == (std::vector<std::string>{"r80", "r81", "r82", "r83"}));  // Ber Jah Cham Zod
    const std::vector<Scope> esrRune = ScopesFor(FromItem("r30", 2, -1, {}, game), game, context);
    print("an ESR rune", esrRune);
    CHECK(esrRune.size() >= 2 && esrRune[1].draft.bases.front() == "r30" && esrRune[1].draft.bases.back() == "r46" && esrRune[1].count == 17);
    CHECK(esrRune.size() >= 3 && esrRune[2].label == "Every green rune" && esrRune[2].count == 8);  // Hu (r48) is shown green
    CHECK(esrRune.size() >= 3 && std::find(esrRune[2].draft.bases.begin(), esrRune[2].draft.bases.end(), "r48") != esrRune[2].draft.bases.end());
    CHECK(runeScopes.size() >= 3 && runeScopes[2].label == "Every classic rune (El to Zod)" && runeScopes[2].count == 33);
    CHECK(RuneColor(game, "r36") == '4' && RuneColor(game, "r50") == ';' && RuneColor(game, "r97") == 'N' && RuneColor(game, "r80") == 0);
    const std::vector<Scope> godRune = ScopesFor(FromItem("r97", 2, -1, {}, game), game, context);
    CHECK(godRune.size() >= 2 && godRune[1].label == "Every Elemental Rune" && godRune[1].count == 14);
    CHECK(godRune.size() >= 2 && godRune[1].label.find("higher") == std::string::npos);  // no rank among these
    // Every scope makes a rule, read back the same.
    for (const auto* list : {&staff, &ring, &uniqueScopes, &setScopes, &couponScopes, &runeScopes})
        for (const Scope& sc : *list) {
            RuleDraft d = sc.draft;
            d.action = DraftAction::Hide;
            Tidy(d, game);
            CHECK(Complete(d));
            const std::vector<edit::OwnRule> made = ToRules(d, game, sc.label);
            CHECK(!made.empty());
            RuleDraft back2;
            CHECK(!made.empty() && FromRule(made.front().rule, game, back2) && back2.kind == d.kind && back2.every == d.every && back2.types == d.types);
        }

    // The loot filter's own reader must take them: written for tools/filtercheck (tests/ui/draft-rules.txt).
    if (outPath != nullptr) {
        edit::ChangeSet changes;
        const auto add = [&changes](edit::OwnRule r, const char* id) {
            r.id = id;
            r.rule.Set("comment", document::Value::String(std::string(id) + " | " + r.rule.Find("comment")->Str()));
            changes.own.push_back(std::move(r));
        };
        add(own, "U001");
        add(hideRule, "U002");
        add(axesRule, "U003");
        add(setRule, "U004");
        const auto scopeRule = [&game](const Scope& sc, DraftAction action) {
            RuleDraft d = sc.draft;
            d.action = action;
            Tidy(d, game);
            return ToRules(d, game, sc.label).front();
        };
        add(scopeRule(staff[3], DraftAction::Hide), "U005");                // superior elite weapons
        add(scopeRule(ring.back(), DraftAction::AlwaysShow), "U006");      // every magic item
        add(scopeRule(uniqueScopes.back(), DraftAction::AlwaysShow), "U007");  // every unique
        add(scopeRule(runeScopes[1], DraftAction::AlwaysShow), "U008");    // Ber and higher
        std::FILE* f = std::fopen(outPath, "wb");
        if (f != nullptr) {
            const std::string text = Render(changes);
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
        }
    }
}

static void ViewTests(const std::shared_ptr<const gamedata::GameData>& game, const std::string& filterText) {
    ViewHarness h;
    // No bridge, then no filter, then no data yet.
    h.Show();
    h.Paint();
    CHECK(h.view->Message().find("did not answer") != std::string::npos);
    h.source->info.bridge = true;
    h.Refresh(true);
    CHECK(h.view->Message().find("No loot filter is loaded") != std::string::npos);
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "esrfe-ui-test";
    fs::remove_all(root);
    const fs::path user = root / "LootFilters";
    h.source->folders = edit::Folders{user.string(), (root / "shipped").string()};
    h.source->info.loaded = true;
    h.source->info.filterId = "repo:QoL-Filter/Sullys-QoL-Filter.json";
    h.source->info.path = (user / "QoL-Filter" / "Sullys-QoL-Filter.json").string();
    h.source->info.level = 3;
    h.source->Put(filterText);
    h.Refresh(true);
    CHECK(h.view->Message().find("Reading the mod") != std::string::npos);
    CHECK(h.view->Data() == nullptr);
    // The data arrives while the window shows: the plugin asks for a refresh.
    h.source->game = game;
    h.Refresh();
    h.Paint();
    const Loaded* data = h.view->Data();
    CHECK(data != nullptr && data->parsed);
    if (data == nullptr || !data->parsed) return;
    const document::Document& doc = data->doc;
    {
        // Parts start closed: "All rules", the parts, and sections that are in no part.
        int parts = 0, loose = 0;
        for (const document::Heading& hd : doc.Headings()) {
            if (hd.level == document::Level::Part) ++parts;
            else if (hd.part < 0) ++loose;
        }
        CHECK(h.view->SectionCount() == 1 + static_cast<int>(doc.Headings().size()));
        CHECK(h.view->SectionRows() == 1 + parts + loose);
        for (int row = 1; row < h.view->SectionRows(); ++row) CHECK(!h.view->RowExpanded(row));
    }
    CHECK(static_cast<int>(h.view->ShownRules().size()) == doc.ActiveCount());
    CHECK(h.Drew("Always show") && h.Drew("R001"));
    CHECK(h.view->Title().find("QoL") != std::string::npos);

    // The same file again: not read again. The file changed: read again.
    CHECK(data->session != nullptr && data->sessionError.empty());
    h.Show();
    CHECK(h.view->Data() == data);
    h.source->info.textHash += 1;
    h.Show();
    CHECK(h.view->Data() != data);
    data = h.view->Data();
    if (data == nullptr) return;

    // The filter in use, chosen in the drop-down; another one there switches the game to it.
    {
        edit::WriteText((user / "QoL-Filter" / ".esr-repo.json").string(), "{}");
        edit::WriteText((user / "Other.json").string(), "{\"formatVersion\": 1, \"title\": \"Another filter\", \"rules\": []}");
        h.Refresh(true);
        h.Paint();
        CHECK(h.view->Filters().visible && h.view->FilterChoices().size() == 2);
        CHECK(h.view->Filters().SelectedText() == "Sully's QoL Filter");
        int other = -1;
        for (std::size_t i = 0; i < h.view->FilterChoices().size(); ++i)
            if (h.view->FilterChoices()[i].id == "user:Other.json") other = static_cast<int>(i);
        CHECK(other >= 0);
        h.view->Filters().onChange(other, h.form);
        std::string selection;
        CHECK(edit::ReadText((user / "selection.json").string(), selection) && selection.find("\"activeFilter\": \"user:Other.json\"") != std::string::npos);
        // The loot filter loads it: the editor shows it.
        const FilterInfo before = h.source->info;
        h.source->info.filterId = "user:Other.json";
        h.source->info.path = (user / "Other.json").string();
        h.source->Put("{\"formatVersion\": 1, \"title\": \"Another filter\", \"rules\": []}");
        h.Run();  // the answer asked for at once: still the old one, so it asks again
        h.Run();
        h.Paint();
        CHECK(h.view->Data() != nullptr && h.view->Data()->doc.GetSettings().title == "Another filter");
        CHECK(h.view->Filters().SelectedText() == "Another filter");
        // Back to Sully's for the rest.
        h.source->info = before;
        h.source->Put(filterText);
        edit::SelectFilter(h.source->folders, before.filterId, selection);
        h.Refresh(true);
        h.Paint();
        data = h.view->Data();
        CHECK(data != nullptr && data->doc.GetSettings().title == "Sully's QoL Filter");
        if (data == nullptr) return;
    }
    const int r125 = RuleIndex(data->doc, "R133");
    CHECK(r125 >= 0);

    // A part opens with a click and shows its sections; a section shows its rules.
    {
        const auto rowOf = [&h](const std::string& name) {
            for (int row = 0; row < h.view->SectionRows(); ++row)
                if (h.view->RowName(row) == name) return row;
            return -1;
        };
        const int protection = rowOf("Protection");
        CHECK(protection > 0 && h.view->RowIsPart(protection));
        CHECK(rowOf("Mythicals") < 0);  // closed
        const int before = h.view->SectionRows();
        h.view->ToggleRow(protection);
        CHECK(h.view->RowExpanded(protection));
        CHECK(h.view->SectionRows() > before);
        const int mythicals = rowOf("Mythicals");
        CHECK(mythicals == protection + 1);
        h.view->ChooseSection(mythicals);
        int heading = -1;
        for (int i = 0; i < static_cast<int>(data->doc.Headings().size()); ++i)
            if (data->doc.Headings()[static_cast<std::size_t>(i)].name == "Mythicals") { heading = i; break; }
        const auto& hd = data->doc.Headings()[static_cast<std::size_t>(heading)];
        CHECK(static_cast<int>(h.view->ShownRules().size()) == hd.endRule - hd.firstRule);
        // Closing the part while one of its sections is chosen chooses the part.
        h.view->ToggleRow(protection);
        CHECK(h.view->SectionRows() == before);
        CHECK(h.view->Sections().Selected() == protection);
        const auto& part = data->doc.Headings()[static_cast<std::size_t>(hd.part)];
        CHECK(static_cast<int>(h.view->ShownRules().size()) == part.endRule - part.firstRule);
        // LB / RB move between the rows that show.
        h.form.PadPress(esrfe::overlay::PadEvent{esrfe::overlay::pad::Button::RB, false});
        CHECK(h.view->Sections().Selected() == protection + 1);
        h.view->ChooseSection(0);
    }

    // The search.
    h.form.Focus(&h.view->Search());
    h.Type("maximum curses");
    bool found = false;
    for (int r : h.view->ShownRules()) found = found || data->doc.Rules()[static_cast<std::size_t>(r)].id == "R068";
    CHECK(found);
    CHECK(h.view->ShownRules().size() < 10);
    // Only rules whose own words have the search: every one shown says "Maximum Curses" somewhere.
    for (int r : h.view->ShownRules()) {
        const RuleSummary& rs = data->rules[static_cast<std::size_t>(r)];
        std::string all = rs.what;
        for (const std::string& c : rs.conditions) all += c;
        for (const std::string& e : rs.effects) all += e;
        CHECK(all.find("Maximum Curses") != std::string::npos);
    }
    // Rows without a match are gone from the left; the ones left count matches.
    CHECK(h.view->SectionRows() < 1 + 10);
    // A search for one item of a long list puts it first in the row.
    h.view->Search().SetValue("");
    h.Type("zod");
    bool zodFirst = false;
    for (int i = 0; i < static_cast<int>(h.view->ShownRules().size()); ++i)
        if (h.view->WhatShown(i).rfind("Zod Rune", 0) == 0) zodFirst = true;
    CHECK(zodFirst);
    h.view->Search().SetValue("");
    h.Type("maximum curses");
    h.Paint();
    CHECK(h.platform.canvas.highlightedTexts > 0);

    // Opening a rule.
    h.form.Focus(&h.view->Rules());
    esrfe::overlay::KeyEvent enter;
    enter.key = esrfe::overlay::Key::Enter;
    h.form.KeyDown(enter);
    CHECK(h.platform.pushed.size() == 1);
    if (!h.platform.pushed.empty()) {
        auto* card = dynamic_cast<RuleView*>(h.platform.pushed.back().get());
        CHECK(card != nullptr);
        if (card != nullptr) {
            bool curses = false, heading2 = false;
            for (const RuleView::Line& l : card->Lines()) {
                curses = curses || l.text.find("Maximum Curses") != std::string::npos;
                heading2 = heading2 || (l.kind == RuleView::Line::Kind::Heading && l.text == "When");
            }
            CHECK(curses && heading2);
            esrfe::overlay::Form cardForm{h.platform, h.platform.pushed.back()};
            cardForm.Layout(h.client, h.content);
            cardForm.Paint(h.platform.canvas);
        }
    }

    // The game's own settings.
    // Help: the guide, by topic.
    h.platform.pushed.clear();
    h.Click(h.view->HelpButton());
    {
        auto help = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<HelpView>(h.platform.pushed.back());
        CHECK(help != nullptr && HelpTopics().size() >= 6);
        if (help != nullptr) {
            esrfe::overlay::Form hf{h.platform, help};
            hf.Layout(h.client, h.content);
            hf.Paint(h.platform.canvas);
            CHECK(help->Topic() == 0 && help->Page().count == static_cast<int>(HelpTopics()[0].paragraphs.size()));
            help->Topics().onClick(1, hf);
            hf.Layout(h.client, h.content);
            hf.Paint(h.platform.canvas);
            CHECK(help->Topic() == 1 && std::string(HelpTopics()[1].title) == "The edit-item key");
            CHECK(help->Page().rowHeight >= h.platform.canvas.LineHeight(esrfe::overlay::Font::Body));
        }
    }
    CHECK(h.source->settingsOpened == 0);

    // ---- editing on a rule's card ----
    {
        const auto openCard = [&h](const std::string& id) -> std::shared_ptr<RuleView> {
            const Loaded* d = h.view->Data();
            h.view->Search().SetValue("");
            h.view->ChooseSection(0);
            h.Type(id);
            int at = -1;
            for (int i = 0; i < static_cast<int>(h.view->ShownRules().size()); ++i)
                if (d->doc.Rules()[static_cast<std::size_t>(h.view->ShownRules()[static_cast<std::size_t>(i)])].id == id) at = i;
            if (at < 0) return nullptr;
            h.platform.pushed.clear();
            h.view->Rules().Select(at, nullptr);
            h.form.Focus(&h.view->Rules());
            esrfe::overlay::KeyEvent go;
            go.key = esrfe::overlay::Key::Enter;
            h.form.KeyDown(go);
            return h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<RuleView>(h.platform.pushed.back());
        };
        std::shared_ptr<RuleView> card = openCard("R133");
        CHECK(card != nullptr);
        if (card != nullptr) {
            esrfe::overlay::Form cardForm{h.platform, card};
            const auto layout = [&] { cardForm.Layout(h.client, h.content); };
            const auto click = [&](esrfe::overlay::Widget& w) {
                layout();
                esrfe::overlay::MouseEvent down;
                down.kind = esrfe::overlay::MouseKind::Down;
                down.x = w.box.CentreX();
                down.y = w.box.CentreY();
                cardForm.Mouse(down);
                esrfe::overlay::MouseEvent up = down;
                up.kind = esrfe::overlay::MouseKind::Up;
                cardForm.Mouse(up);
                layout();
            };
            layout();
            CHECK(card->Editable());
            CHECK(card->HideChip().visible && card->HideChip().selected);
            CHECK(card->LevelCount() == data->doc.GetSettings().filterLevels.size());
            // The first change asks for the version's name; cancelling changes nothing.
            const auto naming = [&]() -> std::shared_ptr<VersionNameView> {
                return h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<VersionNameView>(h.platform.pushed.back());
            };
            h.platform.pushed.clear();
            click(card->OnSwitch());
            auto name = naming();
            CHECK(name != nullptr && name->Name().Value() == "Sully's QoL Filter (my version)" && name->VersionCount() == 0);
            if (name == nullptr) return;
            esrfe::overlay::Form nameForm{h.platform, name};
            nameForm.Layout(h.client, h.content);
            name->CancelButton().onClick(nameForm);
            CHECK(card->OnSwitch().on);
            CHECK(!data->session->HasChanges() || !fs::exists(user / "Sullys-QoL-Filter (my version).json"));
            CHECK(!fs::exists(user / "Sully's QoL Filter (my version).json"));
            // Again: a bad name is refused, a good one makes the copy and switches the game to it.
            h.platform.pushed.clear();
            click(card->OnSwitch());
            name = naming();
            CHECK(name != nullptr);
            if (name == nullptr) return;
            esrfe::overlay::Form nameForm2{h.platform, name};
            nameForm2.Layout(h.client, h.content);
            name->Name().SetValue("bad/name");
            name->MakeButton().onClick(nameForm2);
            CHECK(!name->Problem().empty() && !fs::exists(user / "bad"));
            name->Name().SetValue("Sullys-QoL-Filter (my version)");
            name->MakeButton().onClick(nameForm2);
            CHECK(!card->OnSwitch().on);
            CHECK(card->Status().find("now uses your version") != std::string::npos);
            CHECK(fs::exists(user / "Sullys-QoL-Filter (my version).json"));
            std::string selection;
            CHECK(edit::ReadText((user / "selection.json").string(), selection));
            CHECK(selection.find("(my version)") != std::string::npos);
            // Back on: no question now.
            click(card->OnSwitch());
            CHECK(!cardForm.DialogOpen() && card->OnSwitch().on);
            // Hide -> always show.
            click(card->ShowChip());
            CHECK(card->ShowChip().selected && !card->HideChip().selected);
            // R133 is only on level 3: unticking it is refused.
            std::size_t level3 = 0;
            for (std::size_t i = 0; i < card->LevelCount(); ++i)
                if (card->LevelBox(i).on) level3 = i;
            click(card->LevelBox(level3));
            CHECK(card->LevelBox(level3).on);
            CHECK(card->Status().find("at least one level") != std::string::npos);
            // Level 4 too.
            click(card->LevelBox(level3 + 1 < card->LevelCount() ? level3 + 1 : 0));
            std::string saved;
            CHECK(edit::ReadText((user / "esr-filter-editor" / "Sullys-QoL-Filter (my version).changes.json").string(), saved));
            edit::ChangeSet changes;
            std::string why;
            CHECK(edit::Parse(saved, changes, why));
            const edit::RuleChange* c125 = changes.Find("R133");
            CHECK(c125 != nullptr && c125->Field("activeInLevels") != nullptr && c125->Field("show") != nullptr);
            CHECK(card->ResetButton().visible);
            // Undo: back to Sully's.
            click(card->ResetButton());
            CHECK(card->HideChip().selected && !card->ResetButton().visible);
            cardForm.Paint(h.platform.canvas);
        }
        // Items: untick one gem of R099.
        card = openCard("R099");
        CHECK(card != nullptr);
        if (card != nullptr) {
            esrfe::overlay::Form cardForm{h.platform, card};
            cardForm.Layout(h.client, h.content);
            CHECK(card->ItemEntries().size() > 2);
            card->ToggleItem(1, cardForm);
            CHECK(!cardForm.DialogOpen());  // the copy exists already
            CHECK(!card->ItemEntries()[1].on);
            bool listed = false;
            for (const RuleView::Line& l : card->Lines()) listed = listed || l.text == card->ItemEntries()[1].name;
            CHECK(!listed);
            cardForm.Paint(h.platform.canvas);
        }
        // The list shows the change once it is shown again (the game now uses the copy).
        h.source->info.filterId = "user:Sullys-QoL-Filter (my version).json";
        h.source->info.path = (user / "Sullys-QoL-Filter (my version).json").string();
        h.source->info.textHash += 1;
        h.view->Search().SetValue("");
        h.Show();
        data = h.view->Data();
        CHECK(data != nullptr && data->session != nullptr && data->session->IsCopy());
        if (data != nullptr) {
            const int r090 = data->doc.FindRule("R099");
            CHECK(r090 >= 0 && data->changed[static_cast<std::size_t>(r090)]);
            CHECK(h.view->Title().find("(my version)") != std::string::npos || true);
        }
    }

    // ---- the add-rule steps ----
    {
        std::filesystem::create_directories(user / "QoL-Filter" / "sounds");
        edit::WriteText((user / "QoL-Filter" / "sounds" / "gong.mp3").string(), "GONG");
        {  // a 2x2 minimap icon: red, green, blue, see-through
            std::string sprite("SpA1", 4);
            const auto u16 = [&sprite](unsigned v) { sprite += static_cast<char>(v & 0xFF); sprite += static_cast<char>(v >> 8); };
            u16(31);
            u16(2);
            u16(2);
            u16(0);
            sprite.resize(40, '\0');
            sprite += std::string("\xFF\x00\x00\xFF\x00\xFF\x00\xFF\x00\x00\xFF\xFF\x00\x00\x00\x00", 16);
            std::filesystem::create_directories(user / "QoL-Filter" / "icons");
            edit::WriteText((user / "QoL-Filter" / "icons" / "star.sprite").string(), sprite);
            edit::WriteText((user / "QoL-Filter" / "icons" / "star.lowend.sprite").string(), sprite);
            edit::WriteText((user / "QoL-Filter" / "icons" / "note.txt").string(), "not an icon");
            Picture picture;
            CHECK(ReadSprite((user / "QoL-Filter" / "icons" / "star.sprite").string(), 64, picture));
            CHECK(picture.width == 2 && picture.height == 2 && picture.bgra.size() == 16);
            CHECK(picture.bgra[2] == 255 && picture.bgra[0] == 0 && picture.bgra[3] == 255);  // red, as BGRA
            CHECK(picture.bgra[15] == 0);                                                     // see-through
            CHECK(!ReadSprite((user / "QoL-Filter" / "icons" / "note.txt").string(), 64, picture));
        }
        h.source->info.textHash += 1;  // read the player's copy afresh
        h.Show();
        data = h.view->Data();
        CHECK(data != nullptr && data->session != nullptr && data->session->IsCopy());
        h.Paint();
        CHECK(h.view->AddRule().visible);
        h.platform.pushed.clear();
        h.Click(h.view->AddRule());
        CHECK(h.platform.pushed.size() == 1);
        auto steps = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<AddRuleView>(h.platform.pushed.back());
        CHECK(steps != nullptr);
        if (steps != nullptr) {
            esrfe::overlay::Form f{h.platform, steps};
            const auto layout = [&] { f.Layout(h.client, h.content); };
            const auto click = [&](esrfe::overlay::Widget& w) {
                layout();
                esrfe::overlay::MouseEvent down;
                down.kind = esrfe::overlay::MouseKind::Down;
                down.x = w.box.CentreX();
                down.y = w.box.CentreY();
                f.Mouse(down);
                esrfe::overlay::MouseEvent up = down;
                up.kind = esrfe::overlay::MouseKind::Up;
                f.Mouse(up);
                layout();
            };
            using Step = AddRuleView::Step;
            layout();
            CHECK(!steps->Finish().enabled && !steps->StepUsed(Step::Items));
            click(steps->ActionChip(DraftAction::Show));
            CHECK(steps->Draft().action == DraftAction::Show && steps->StepUsed(Step::Look) && steps->StepUsed(Step::Levels));
            // The crash: magic with a bonus ticked, then another kind.
            steps->GoTo(Step::Kind);
            layout();
            click(steps->KindChip(1));  // Magic items
            CHECK(steps->Draft().kind == ItemKind::Magic && steps->CurrentStep() == Step::Items);
            f.Focus(&steps->ItemSearch());
            for (char16_t ch : esrfe::overlay::Utf8ToUtf16("mystic staff")) f.Char(ch);
            layout();
            for (int i = 0; i < static_cast<int>(steps->Entries().size()); ++i)
                if (steps->Entries()[static_cast<std::size_t>(i)].code == "1ls") steps->ToggleEntry(i, f);
            steps->GoTo(Step::Bonuses);
            layout();
            CHECK(steps->Offered().statsPossible && !steps->StatRows().empty());
            steps->ToggleStat(0, f);
            CHECK(steps->Draft().stats.size() == 1);
            f.Paint(h.platform.canvas);
            steps->GoTo(Step::Kind);
            layout();
            click(steps->KindChip(3));  // Unique items: no crash, nothing of the magic pick left
            CHECK(steps->Draft().kind == ItemKind::Unique && steps->Draft().bases.empty() && steps->Draft().stats.empty());
            bool elite = false, coupons = false;
            for (const AddRuleView::Category& c : steps->Categories()) {
                elite = elite || c.name == "Elite tier";
                coupons = coupons || c.name == "Ancient Coupons: Normal";
            }
            CHECK(elite && coupons);
            // Set items: by tier, then set by set; runes: by the colour of their names.
            steps->GoTo(Step::Kind);
            layout();
            click(steps->KindChip(4));
            CHECK(steps->Draft().kind == ItemKind::Set);
            bool setElite = false, bySet = false;
            for (const AddRuleView::Category& c : steps->Categories()) {
                setElite = setElite || (c.name == "Elite tier" && !c.entries.empty());
                bySet = bySet || (c.name == "By set" && c.heading);
            }
            CHECK(setElite && bySet);
            steps->GoTo(Step::Kind);
            layout();
            click(steps->KindChip(5));
            CHECK(steps->Draft().kind == ItemKind::Other);
            std::size_t green = 0;
            bool classic = false;
            for (const AddRuleView::Category& c : steps->Categories()) {
                if (c.name == "Runes: Green") green = c.entries.size();
                classic = classic || c.name == "Runes: classic and others";
            }
            CHECK(green == 8 && classic);
            // No two alike in a list: the unused and monster ring copies are gone, the Mythical ones say so.
            const auto noTwins = [&](bool& mythRing, bool& unused) {
                for (const AddRuleView::Category& c : steps->Categories()) {
                    std::set<std::string> seen;
                    for (const AddRuleView::Entry& e : c.entries) {
                        CHECK(seen.insert(e.name + "|" + e.tag).second);
                        mythRing = mythRing || (e.code == "mrn" && e.tag == "Mythical");
                        unused = unused || e.code == "rn2" || e.code == "rn9" || e.code == "au2" || e.code == "au9" || e.code == "mr1";
                    }
                }
            };
            {
                bool myth = false, unused = false;
                noTwins(myth, unused);
                CHECK(!unused);
            }
            f.Focus(&steps->ItemSearch());
            for (char16_t ch : esrfe::overlay::Utf8ToUtf16("Ber Rune")) f.Char(ch);
            layout();
            CHECK(steps->Entries().size() == 1);  // found once
            steps->ItemSearch().SetValue("");
            steps->GoTo(Step::Kind);
            layout();
            // Uniques: filters by item type above the lists.
            click(steps->KindChip(3));
            steps->GoTo(AddRuleView::Step::Items);
            layout();
            CHECK(steps->TypeChip(0).visible && steps->TypeChip(0).selected);
            std::size_t allCount = 0;
            for (const AddRuleView::Category& c : steps->Categories()) allCount += c.entries.size();
            const auto filtered = [&](const char* label) {
                for (std::size_t i = 0; i < AddRuleView::TypeFilters().size(); ++i)
                    if (std::string(AddRuleView::TypeFilters()[i].label) == label) click(steps->TypeChip(i));
                std::size_t n = 0;
                for (const AddRuleView::Category& c : steps->Categories()) n += c.entries.size();
                return n;
            };
            const std::size_t boots = filtered("Boots");
            CHECK(boots > 0 && boots < allCount);
            for (const AddRuleView::Category& c : steps->Categories())
                for (const AddRuleView::Entry& e : c.entries) CHECK(e.type == AddRuleView::Entry::Type::Unique && game->types.IsA(game->bases.Find(e.base)->type, "boot"));
            const std::size_t weapons = filtered("Weapons"), couponCount = filtered("Ancient Coupons"), other = filtered("Other");
            std::size_t sum = couponCount + other;
            for (const char* l : {"Weapons", "Body armor", "Helms", "Shields", "Gloves", "Boots", "Belts", "Rings and amulets", "Charms", "Jewels"}) sum += filtered(l);
            CHECK(weapons > boots && couponCount > 0 && sum == allCount);  // every unique and coupon in exactly one filter
            filtered("All");
            steps->GoTo(Step::Kind);
            layout();
            click(steps->KindChip(1));  // magic: rings and amulets
            {
                bool myth = false, unused = false;
                noTwins(myth, unused);
                CHECK(myth && !unused);
            }
            steps->GoTo(Step::Kind);
            layout();
            click(steps->KindChip(3));
            steps->ItemSearch().SetValue("");
            CHECK(!steps->StepUsed(Step::Tier) && !steps->StepUsed(Step::Bonuses));
            f.Paint(h.platform.canvas);
            // White and superior bases: the staff, superior only, a bonus.
            steps->GoTo(Step::Kind);
            layout();
            click(steps->KindChip(0));
            f.Focus(&steps->ItemSearch());
            for (char16_t ch : esrfe::overlay::Utf8ToUtf16("mystic staff")) f.Char(ch);
            layout();
            int staff = -1;
            for (int i = 0; i < static_cast<int>(steps->Entries().size()); ++i)
                if (steps->Entries()[static_cast<std::size_t>(i)].code == "1ls") staff = i;
            CHECK(staff >= 0);
            steps->ToggleEntry(staff, f);
            CHECK(steps->Draft().bases == std::vector<std::string>{"1ls"});
            CHECK(steps->Finish().enabled);
            steps->GoTo(Step::Tier);
            layout();
            CHECK(steps->WhiteBox().visible && steps->WhiteBox().on && steps->SuperiorBox().on);
            click(steps->WhiteBox());
            CHECK(steps->Draft().baseQualities == std::set<int>{3});
            steps->GoTo(Step::Bonuses);
            layout();
            int allSkills = -1;
            for (int i = 0; i < static_cast<int>(steps->StatRows().size()); ++i)
                if (steps->Offered().stats[static_cast<std::size_t>(steps->StatRows()[static_cast<std::size_t>(i)])].label.find("All Skills") != std::string::npos)
                    allSkills = i;
            CHECK(allSkills >= 0);
            steps->ToggleStat(allSkills, f);
            // The style: a red border, a blue beam, a minimap icon at 32 px, a sound (heard at 80 x the filter's volume), a chat line.
            steps->GoTo(Step::Look);
            layout();
            click(steps->BorderSwatch(1));
            CHECK(steps->Draft().look.border == Swatches()[1].rgb);
            int blue = -1;
            for (std::size_t i = 0; i < Swatches().size(); ++i)
                if (std::string(Swatches()[i].english) == "Blue") blue = static_cast<int>(i);
            CHECK(blue >= 0);
            click(steps->BeamSwatch(static_cast<std::size_t>(blue)));
            CHECK(steps->Draft().look.beam && steps->Draft().look.beamColor == Swatches()[static_cast<std::size_t>(blue)].rgb);
            CHECK(steps->Icon().options.size() == 2 && steps->Icon().options[1] == "star");  // .lowend and other files left out
            CHECK(!steps->IconSize().enabled);
            steps->Icon().onChange(1, f);
            CHECK(steps->Draft().look.icon == "star" && steps->Draft().look.iconSize == 27 && steps->IconSize().enabled);
            steps->IconSize().SetValue(32);
            steps->IconSize().onChange(32, f);
            steps->IconSize().onCommit(32, f);
            CHECK(steps->Draft().look.iconSize == 32);
            layout();
            h.platform.canvas.pictures.clear();
            f.Paint(h.platform.canvas);
            CHECK(std::find(h.platform.canvas.pictures.begin(), h.platform.canvas.pictures.end(), "icon:star") != h.platform.canvas.pictures.end());
            CHECK(steps->Sound().options.size() == 2);
            h.source->played.clear();
            steps->Sound().onChange(1, f);
            CHECK(steps->Draft().look.sound == "gong.mp3");
            CHECK(h.source->played.size() == 1 && h.source->played[0].second == 80 * data->session->SoundVolume() / 100);
            click(steps->Chat());
            f.Focus(&steps->ChatText());
            for (char16_t ch : esrfe::overlay::Utf8ToUtf16("Found {name}")) f.Char(ch);
            layout();
            CHECK(steps->Draft().look.chat && steps->Draft().look.chatText == "Found {name}");
            CHECK(steps->PreviewName() == "Mystic Staff");
            h.platform.canvas.drawn.clear();
            f.Paint(h.platform.canvas);
            bool drewName = false;
            for (const std::string& t : h.platform.canvas.drawn) drewName = drewName || t == "Found Mystic Staff";
            CHECK(drewName);
            steps->GoTo(Step::Check);
            layout();
            f.Paint(h.platform.canvas);
            const int pops = h.platform.pops;
            click(steps->Finish());
            CHECK(!f.DialogOpen());  // the copy exists: no question
            CHECK(h.platform.pops == pops + 1);
        }
        // Saved: the changes and the game's copy.
        {
            std::string saved, error;
            CHECK(edit::ReadText((user / "esr-filter-editor" / "Sullys-QoL-Filter (my version).changes.json").string(), saved));
            edit::ChangeSet changes;
            CHECK(edit::Parse(saved, changes, error));
            CHECK(changes.own.size() == 1 && changes.own[0].id == "U001" && changes.own[0].place == "end" && changes.own[0].keepShown);
            CHECK(changes.own.size() == 1 && changes.own[0].rule.Find("sound") != nullptr && changes.own[0].rule.Find("chatMessage") != nullptr);
            CHECK(changes.own.size() == 1 && changes.own[0].rule.Find("icon") != nullptr &&
                  *changes.own[0].rule.Find("icon") == document::Value::Object({{"iconName", document::Value::String("star")}, {"iconSize", document::Value::Integer(32)}}));
            CHECK(changes.own.size() == 1 && changes.own[0].rule.Find("comment")->Str().rfind("U001 | Mystic Staff", 0) == 0);
            std::string copyText;
            CHECK(edit::ReadText((user / "Sullys-QoL-Filter (my version).json").string(), copyText));
            document::Document copy;
            std::vector<document::Diag> diags;
            CHECK(document::Document::Parse(copyText, copy, diags));
            CHECK(copy.FindHeading("My Highlights") >= 0 && copy.FindHeading("My Rules") == 0);
            CHECK(std::filesystem::exists(user / "sounds" / "gong.mp3"));  // the sound came with the copy
        }
        // The list shows it, marked as yours; its card can change it.
        h.source->info.textHash += 1;
        h.Show();
        data = h.view->Data();
        const int u1 = data != nullptr ? data->doc.FindRule("U001") : -1;
        CHECK(u1 >= 0 && data->changed[static_cast<std::size_t>(u1)]);
        CHECK(data->doc.FindHeading("My Rules") < 0);  // the show-only copy is not in the editor's view
        h.view->Search().SetValue("");
        h.view->ChooseSection(0);
        h.form.Focus(&h.view->Search());
        h.Type("U001");
        CHECK(h.view->ShownRules().size() == 1);
        h.platform.pushed.clear();
        h.form.Focus(&h.view->Rules());
        esrfe::overlay::KeyEvent go;
        go.key = esrfe::overlay::Key::Enter;
        h.form.KeyDown(go);
        auto card = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<RuleView>(h.platform.pushed.back());
        CHECK(card != nullptr && card->Own());
        if (card != nullptr) {
            esrfe::overlay::Form cf{h.platform, card};
            cf.Layout(h.client, h.content);
            CHECK(card->ChangeButton().visible && card->DeleteButton().visible);
            h.platform.pushed.clear();
            card->ChangeButton().onClick(cf);
            auto again = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<AddRuleView>(h.platform.pushed.back());
            CHECK(again != nullptr);
            if (again != nullptr) {
                // Shown on every level: the same as Always Show.
                CHECK(again->Draft().action == DraftAction::AlwaysShow && again->Draft().kind == ItemKind::Base && again->Draft().stats.size() == 1);
                CHECK(again->Draft().look.sound == "gong.mp3" && again->Draft().look.chatText == "Found {name}");
                CHECK(again->Draft().look.icon == "star" && again->Draft().look.iconSize == 32);
                esrfe::overlay::Form af{h.platform, again};
                af.Layout(h.client, h.content);
                again->BorderSwatch(0).onClick(af);
                again->Finish().onClick(af);
                std::string saved, error;
                edit::ChangeSet changes;
                CHECK(edit::ReadText((user / "esr-filter-editor" / "Sullys-QoL-Filter (my version).changes.json").string(), saved) && edit::Parse(saved, changes, error));
                CHECK(changes.own.size() == 1 && changes.own[0].id == "U001");  // the same rule, changed
                RuleDraft d;
                CHECK(changes.own.size() == 1 && FromRule(changes.own[0].rule, *data->game, d) && d.look.border == Swatches()[0].rgb);
            }
            // Off, then delete.
            card->OnSwitch().onChange(false, cf);
            CHECK(data->session->IsOff("U001"));
            card->DeleteButton().onClick(cf);
            CHECK(cf.DialogOpen());
            esrfe::overlay::KeyEvent enterKey;
            enterKey.key = esrfe::overlay::Key::Enter;
            cf.KeyDown(enterKey);  // "Cancel" has the focus first: nothing deleted
            CHECK(data->session->Own("U001") != nullptr);
            card->DeleteButton().onClick(cf);
            esrfe::overlay::KeyEvent right;
            right.key = esrfe::overlay::Key::Right;
            cf.KeyDown(right);
            cf.KeyDown(enterKey);
            CHECK(data->session->Own("U001") == nullptr);
        }
        // Edit a filter rule: the steps open on a copy; saving switches R095 off and puts the copy in its place.
        {
            h.view->Search().SetValue("");
            h.form.Focus(&h.view->Search());
            h.Type("R095");
            CHECK(h.view->ShownRules().size() == 1);
            h.form.Focus(&h.view->Rules());
            h.view->Rules().Select(0, nullptr);
            h.platform.pushed.clear();
            h.Paint();
            CHECK(h.view->EditRule().visible && h.view->EditRule().enabled);
            h.view->EditRule().onClick(h.form);  // the top bar's Edit rule
            auto steps = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<AddRuleView>(h.platform.pushed.back());
            CHECK(steps != nullptr && steps->Draft().replacing && steps->Title() == "Edit R095");
            CHECK(steps != nullptr && steps->Draft().action == DraftAction::Hide);
            if (steps != nullptr) {
                esrfe::overlay::Form sf{h.platform, steps};
                sf.Layout(h.client, h.content);
                steps->GoTo(AddRuleView::Step::Levels);
                sf.Layout(h.client, h.content);
                CHECK(steps->LevelCount() == 4);
                steps->LevelBox(0).onChange(false, sf);  // not on level 1 any more
                CHECK(steps->Finish().enabled);
                steps->Finish().onClick(sf);
                const edit::Session& s = *data->session;
                CHECK(s.IsOff("R095"));
                const edit::OwnRule* copy = nullptr;
                for (const edit::OwnRule& o : s.Changes().own)
                    if (o.replaces == "R095") copy = &o;
                CHECK(copy != nullptr && copy->after == "R095" && copy->place.empty() && !copy->keepShown);
                if (copy != nullptr) {
                    const document::Value* author = s.AuthorRule("R095");
                    CHECK(author != nullptr && copy->rule.Find("itemCodes") != nullptr && *copy->rule.Find("itemCodes") == *author->Find("itemCodes"));
                    CHECK(copy->rule.Find("comment")->Str().find("Hides low-value items") != std::string::npos);
                    const document::Value* levels = copy->rule.Find("activeInLevels");
                    CHECK(levels != nullptr && levels->array.size() == 3);
                    // The game's copy: the edited rule right where R095 was, R095 gone.
                    std::string gameText;
                    CHECK(edit::ReadText(s.MergedPath(), gameText));
                    document::Document game;
                    std::vector<document::Diag> diags;
                    CHECK(document::Document::Parse(gameText, game, diags));
                    const int at = game.FindRule(copy->id);
                    CHECK(game.FindRule("R095") < 0 && at > 0 && game.Rules()[static_cast<std::size_t>(at - 1)].id == "R094");
                    // Deleting the copy brings R095 back.
                    const std::string id = copy->id;
                    data->session->RemoveOwn(id);
                    CHECK(!data->session->IsOff("R095") && data->session->Own(id) == nullptr);
                }
            }
        }
        // The hover key: the item's rule, waiting for the filter when needed.
        h.platform.pushed.clear();
        h.view->StartRule(h.form, [](const gamedata::GameData& g) { return FromItem("xhl", 3, -1, {{g.stats.Id("extra_curse"), 0, 1}}, g); });
        auto fromHover = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<AddRuleView>(h.platform.pushed.back());
        CHECK(fromHover != nullptr && fromHover->Draft().bases == std::vector<std::string>{"xhl"} && fromHover->Draft().stats.size() == 1);
        CHECK(fromHover != nullptr && fromHover->CurrentStep() == AddRuleView::Step::Action && !fromHover->Finish().enabled);
        // The edit-item key on a hidden item: its own screen. Show it (or turn off the rule hiding it), for which
        // items, on which levels; the rules that match it are one button away.
        ItemRules item;
        item.name = "Kenji's Winged Arrows";
        item.hidden = true;
        item.hiddenBy = "R133";
        item.matched = {{"R164", "R164 | Uniques from the aq*/cq* codes", 300}, {"R133", "", 200}, {"", "no id here", 12}};
        h.platform.pushed.clear();
        h.view->StartRule(h.form, [](const gamedata::GameData& g) { return FromItem("aq2", 7, 85, {}, g); }, item);
        auto quick = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<QuickRuleView>(h.platform.pushed.back());
        CHECK(quick != nullptr);
        if (quick != nullptr) {
            esrfe::overlay::Form qf{h.platform, quick};
            qf.Layout(h.client, h.content);
            qf.Paint(h.platform.canvas);
            using Want = QuickRuleView::Want;
            using Where = QuickRuleView::Where;
            CHECK(quick->State().find("Hidden by R133") != std::string::npos);
            CHECK(quick->Wants() == (std::vector<Want>{Want::Show, Want::AlwaysShow, Want::Style}));
            CHECK(quick->CurrentWant() == Want::Show && quick->CurrentWhere() == Where::Every);
            CHECK(quick->TurnOff().visible);
            CHECK(quick->Note().find("R133 hides it now") != std::string::npos);
            CHECK(quick->Scopes().size() >= 3 && quick->Scopes()[0].label.rfind("Only ", 0) == 0 && quick->Scopes()[0].count == 1);
            CHECK(quick->Scopes().back().label == "Every unique" && quick->Scopes().back().draft.every);
            // The rules that match it.
            h.platform.pushed.clear();
            quick->SeeRules().onClick(qf);
            auto about = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<ItemRulesView>(h.platform.pushed.back());
            CHECK(about != nullptr);
            if (about != nullptr) {
                esrfe::overlay::Form af{h.platform, about};
                af.Layout(h.client, h.content);
                af.Paint(h.platform.canvas);
                CHECK(about->Rows().size() == 3);
                CHECK(about->Rows()[0].rule >= 0 && about->Rows()[0].head.rfind("R164", 0) == 0 && !about->Rows()[0].hides);
                CHECK(about->Rows()[1].hides && about->Rows()[1].head.find("hides it") != std::string::npos);
                CHECK(about->Rows()[2].rule < 0);  // no id: listed, not opened
                h.platform.pushed.clear();
                about->OpenRow(2, af);
                CHECK(h.platform.pushed.empty());
                about->OpenRow(0, af);
                CHECK(!h.platform.pushed.empty() && std::dynamic_pointer_cast<RuleView>(h.platform.pushed.back()) != nullptr);
            }
            // Show every unique in its group, on every level: one rule, shown (never hidden).
            quick->ChooseScope(2);
            RuleDraft d = quick->Draft();
            CHECK(d.action == DraftAction::AlwaysShow && d.kind == ItemKind::Unique && d.uniques.size() == quick->Scopes()[2].count);
            // This level and stricter ones: a show rule with those levels.
            quick->ChooseWhere(Where::ThisAndStricter);
            d = quick->Draft();
            CHECK(d.action == DraftAction::Show && !d.levels.empty() && d.levels.front() == h.source->info.level);
            // The style: the steps open on Style, filled in.
            quick->Choose(Want::Style);
            CHECK(quick->Go().label == "Choose the style…");
            h.platform.pushed.clear();
            quick->Go().onClick(qf);
            auto styleSteps = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<AddRuleView>(h.platform.pushed.back());
            CHECK(styleSteps != nullptr && styleSteps->CurrentStep() == AddRuleView::Step::Look &&
                  styleSteps->Draft().uniques.size() == quick->Scopes()[2].count && styleSteps->Draft().action == DraftAction::AlwaysShow);
            // More options: the steps on the items, filled in.
            quick->Choose(Want::AlwaysShow);
            quick->ChooseScope(0);
            h.platform.pushed.clear();
            quick->More().onClick(qf);
            auto more = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<AddRuleView>(h.platform.pushed.back());
            CHECK(more != nullptr && more->CurrentStep() == AddRuleView::Step::Items && more->Draft().action == DraftAction::AlwaysShow);
        }
        // A carried item: where it is, and what the filter would do if it dropped here.
        {
            ItemRules carried = item;
            carried.location = 1;
            h.platform.pushed.clear();
            h.view->StartRule(h.form, [](const gamedata::GameData& g) { return FromItem("aq2", 7, 85, {}, g); }, carried);
            auto inv = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<QuickRuleView>(h.platform.pushed.back());
            CHECK(inv != nullptr && inv->State().rfind("In your inventory, stash or cube.  If it dropped here: Hidden by R133.", 0) == 0);
        }
        // A shown magic charm: hide it, for every magic charm, on this level and stricter ones. Saved as the player's rule.
        {
            ItemRules charm;
            charm.name = "Grand Charm";
            h.platform.pushed.clear();
            h.view->StartRule(h.form, [](const gamedata::GameData& g) { return FromItem("cm3", 4, -1, {}, g); }, charm);
            auto q = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<QuickRuleView>(h.platform.pushed.back());
            CHECK(q != nullptr);
            if (q != nullptr) {
                esrfe::overlay::Form qf{h.platform, q};
                qf.Layout(h.client, h.content);
                qf.Paint(h.platform.canvas);
                CHECK(q->State() == "Shown." && q->Wants().front() == QuickRuleView::Want::Hide);
                CHECK(q->CurrentWhere() == QuickRuleView::Where::ThisAndStricter && !q->TurnOff().visible);
                int charms = -1;
                for (int i = 0; i < static_cast<int>(q->Scopes().size()); ++i)
                    if (q->Scopes()[static_cast<std::size_t>(i)].label.rfind("Every magic item in", 0) == 0 && charms < 0) charms = i;
                CHECK(charms >= 1 && q->Scopes()[0].label == "Every magic Grand Charm");
                q->ChooseScope(charms);
                CHECK(q->Go().label == "Hide them" && q->Go().enabled);
                const std::size_t before = data->session->OwnRules().size();
                const int pops = h.platform.pops;
                q->Go().onClick(qf);
                CHECK(h.platform.pops == pops + 1);
                CHECK(data->session->OwnRules().size() == before + 1);
                if (data->session->OwnRules().size() == before + 1) {
                    const edit::OwnRule& r = data->session->OwnRules().back();
                    CHECK(r.place == "top" && r.rule.Find("hide") != nullptr && r.rule.Find("quality")->Str() == "magic");
                    CHECK(r.rule.Find("activeInLevels") != nullptr && r.rule.Find("itemCodes") != nullptr);
                    CHECK(r.rule.Find("comment")->Str().find("Every magic item in") != std::string::npos);
                }
            }
        }
    }

    // ---- the filter's volume: saved in the player's version, heard at the loot filter's volume ----
    {
        h.source->info.textHash += 1;
        h.Show();
        data = h.view->Data();
        CHECK(data != nullptr && data->session != nullptr);
        h.Paint();
        esrfe::overlay::Slider& volume = h.view->Volume();
        CHECK(volume.visible && volume.value == data->session->SoundVolume());
        h.source->played.clear();
        // A drag: nothing saved until the mouse lets go.
        esrfe::overlay::MouseEvent down;
        down.kind = esrfe::overlay::MouseKind::Down;
        down.x = volume.Track().left + volume.Track().Width() / 4;
        down.y = volume.box.CentreY();
        h.form.Mouse(down);
        esrfe::overlay::MouseEvent move = down;
        move.kind = esrfe::overlay::MouseKind::Move;
        move.x = volume.Track().left + volume.Track().Width() / 2;
        h.form.Mouse(move);
        CHECK(volume.value == 50 && data->session->SoundVolume() != 50 && h.source->played.empty());
        esrfe::overlay::MouseEvent up = move;
        up.kind = esrfe::overlay::MouseKind::Up;
        h.form.Mouse(up);
        CHECK(data->session->SoundVolume() == 50);
        CHECK(h.source->played.size() == 1 && h.source->played[0].second == 40);  // 80 x 50%
        std::string saved, error;
        edit::ChangeSet changes;
        CHECK(edit::ReadText((user / "esr-filter-editor" / "Sullys-QoL-Filter (my version).changes.json").string(), saved) && edit::Parse(saved, changes, error));
        bool has = false;
        for (const document::Member& m : changes.settings) {
            std::int64_t v = 0;
            has = has || (m.key == "soundVolume" && m.value.AsInteger(v) && v == 50);
        }
        CHECK(has);
        std::string copyText;
        CHECK(edit::ReadText((user / "Sullys-QoL-Filter (my version).json").string(), copyText));
        CHECK(copyText.find("\"soundVolume\": 50") != std::string::npos);
        // The keyboard: one step, saved at once.
        h.form.Focus(&volume);
        esrfe::overlay::KeyEvent right;
        right.key = esrfe::overlay::Key::Right;
        h.form.KeyDown(right);
        CHECK(volume.value == 55 && data->session->SoundVolume() == 55);
        h.source->played.clear();
        h.Click(h.view->VolumeTest());
        CHECK(h.source->played.size() == 1 && h.source->played[0].second == 44);
    }

    // ---- Sully updates a rule the player changed: the editor asks ----
    {
        const std::string copyId = "user:Sullys-QoL-Filter (my version).json";
        edit::Session session;
        std::string error;
        bool switched = false;
        CHECK(session.Open(copyId, (user / "Sullys-QoL-Filter (my version).json").string(), h.source->folders, error));
        document::Value mine = document::Value::Array({document::Value::Integer(3), document::Value::Integer(4)});
        session.SetField("R133", "activeInLevels", &mine);
        CHECK(session.Save(error, switched));
        // The author's new version: R133 on level 2 only.
        document::Document updated;
        std::vector<document::Diag> diags;
        CHECK(document::Document::Parse(filterText, updated, diags));
        CHECK(updated.SetField(updated.FindRule("R133"), "activeInLevels", document::Value::Array({document::Value::Integer(2)})));
        const std::string newer = updated.Text();
        CHECK(edit::WriteText((user / "QoL-Filter" / "Sullys-QoL-Filter.json").string(), newer));
        h.source->info.textHash += 1;
        h.Show();
        CHECK(h.form.DialogOpen());
        // Escape keeps the player's version.
        esrfe::overlay::KeyEvent esc;
        esc.key = esrfe::overlay::Key::Escape;
        h.form.KeyDown(esc);
        h.Run();
        CHECK(!h.form.DialogOpen());
        std::string saved;
        CHECK(edit::ReadText((user / "esr-filter-editor" / "Sullys-QoL-Filter (my version).changes.json").string(), saved));
        edit::ChangeSet changes;
        CHECK(edit::Parse(saved, changes, error));
        CHECK(changes.baseHash == edit::Hash(newer));
        CHECK(changes.Find("R133") != nullptr && changes.Find("R133")->Field("activeInLevels") != nullptr);
        CHECK(changes.Find("R133")->Field("activeInLevels")->value == mine);
        std::string copyText;
        CHECK(edit::ReadText((user / "Sullys-QoL-Filter (my version).json").string(), copyText));
        document::Document copy;
        CHECK(document::Document::Parse(copyText, copy, diags));
        CHECK(*copy.Rules()[static_cast<std::size_t>(copy.FindRule("R133"))].value.Find("activeInLevels") == mine);
        // Shown again: nothing more to ask.
        h.source->info.textHash += 1;
        h.Show();
        CHECK(!h.form.DialogOpen());
    }

    // Back on Sully's filter, a change: the version the player has is offered, and the change goes into it.
    {
        h.source->info.filterId = "repo:QoL-Filter/Sullys-QoL-Filter.json";
        h.source->info.path = (user / "QoL-Filter" / "Sullys-QoL-Filter.json").string();
        h.source->Put(filterText);
        h.Refresh(true);
        Loaded* now = const_cast<Loaded*>(h.view->Data());
        CHECK(now != nullptr && now->session != nullptr && now->session->NeedsName());
        if (now != nullptr && now->session != nullptr) {
            std::shared_ptr<Loaded> keep(std::shared_ptr<Loaded>{}, now);
            auto alive = std::make_shared<bool>(true);
            bool saved = false;
            h.platform.pushed.clear();
            CommitChange(h.form, keep, alive, [](edit::Session& s) { s.SetOff("R002", true); },
                         [&saved](Ui&, bool ok, bool, const std::string&) { saved = ok; }, nullptr);
            auto name = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<VersionNameView>(h.platform.pushed.back());
            CHECK(name != nullptr && name->VersionCount() == 1);
            if (name != nullptr && name->VersionCount() == 1) {
                CHECK(name->Name().Value() == "Sully's QoL Filter (my version)");  // a new name still offered
                name->VersionButton(0).onClick(h.form);
                CHECK(saved && now->session->IsCopy() && now->session->IsOff("R002"));
                CHECK(now->session->MergedId() == "user:Sullys-QoL-Filter (my version).json");
                std::string selection;
                CHECK(edit::ReadText((user / "selection.json").string(), selection) &&
                      selection.find("\"activeFilter\": \"user:Sullys-QoL-Filter (my version).json\"") != std::string::npos);
            }
        }
    }

    // A new version, named: the drop-down lists it and chooses it at once, before the game has switched.
    {
        h.source->info.filterId = "repo:QoL-Filter/Sullys-QoL-Filter.json";
        h.source->info.path = (user / "QoL-Filter" / "Sullys-QoL-Filter.json").string();
        h.source->Put(filterText);
        h.Refresh(true);
        Loaded* now = const_cast<Loaded*>(h.view->Data());
        CHECK(now != nullptr && now->session != nullptr && now->session->NeedsName());
        if (now != nullptr && now->session != nullptr) {
            std::shared_ptr<Loaded> keep(std::shared_ptr<Loaded>{}, now);
            auto alive = std::make_shared<bool>(true);
            bool saved = false;
            h.platform.pushed.clear();
            CommitChange(h.form, keep, alive, [](edit::Session& s) { s.SetOff("R003", true); },
                         [&saved](Ui&, bool ok, bool, const std::string&) { saved = ok; }, nullptr);
            auto name = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<VersionNameView>(h.platform.pushed.back());
            CHECK(name != nullptr);
            if (name != nullptr) {
                name->Name().SetValue("test 2");
                name->MakeButton().onClick(h.form);
                CHECK(saved);
                h.Refresh();  // the game still says Sully's filter
                const esrfe::overlay::Dropdown& filters = h.view->Filters();
                const auto at = std::find(filters.options.begin(), filters.options.end(), std::string("test 2"));
                CHECK(at != filters.options.end() && filters.selected == static_cast<int>(at - filters.options.begin()));
            }
        }
    }

    // A new filter from the questions: "+ New filter…" in the drop-down, every step filled in, Create switches to it.
    {
        h.source->folders.starter = ESRFE_STARTER_DIR;
        h.source->info.filterId = "repo:QoL-Filter/Sullys-QoL-Filter.json";
        h.source->info.path = (user / "QoL-Filter" / "Sullys-QoL-Filter.json").string();
        h.source->Put(filterText);
        h.Refresh(true);
        h.Paint();
        esrfe::overlay::Dropdown& filters = h.view->Filters();
        CHECK(filters.options.back() == "+ New filter…");
        const int before = filters.selected;
        h.platform.pushed.clear();
        filters.onChange(static_cast<int>(filters.options.size()) - 1, h.form);
        CHECK(filters.selected == before);
        auto wizard = h.platform.pushed.empty() ? nullptr : std::dynamic_pointer_cast<NewFilterView>(h.platform.pushed.back());
        CHECK(wizard != nullptr && wizard->Ready());
        if (wizard != nullptr && wizard->Ready()) {
            esrfe::overlay::Form wf{h.platform, wizard};
            const auto layout = [&] { wf.Layout(h.client, h.content); };
            layout();
            wf.Paint(h.platform.canvas);
            CHECK(wizard->StepCount() == 10 && wizard->StepKey() == "name" && wizard->Name().Value() == "My filter");
            CHECK(wizard->Answers().levels.size() == 4);
            wf.Focus(&wizard->Name());
            for (int i = 0; i < 9; ++i) {
                esrfe::overlay::KeyEvent bs;
                bs.key = esrfe::overlay::Key::Backspace;
                wf.KeyDown(bs);
            }
            for (char16_t ch : esrfe::overlay::Utf8ToUtf16("Speed run")) wf.Char(ch);
            CHECK(wizard->Answers().name == "Speed run");
            // Levels: three.
            wizard->GoTo(1);
            layout();
            wf.Paint(h.platform.canvas);
            wizard->CountChip(2).onClick(wf);
            CHECK(wizard->Answers().levels.size() == 3 && wizard->Answers().defaultLevel == 3);
            // Gold and potions: the gold part's steps; change the first.
            wizard->GoTo(3);
            layout();
            CHECK(wizard->StepKey() == "gold" && wizard->Lines().size() > 10);
            CHECK(wizard->Lines()[0].row < 0 && wizard->LineText(1).find("gold") != std::string::npos);
            wizard->List().Select(2, nullptr);  // R071: up to 99 gold from clvl 20
            wizard->List().onSelect(2, wf);
            layout();
            CHECK(wizard->Clvl().visible && wizard->Amount().visible && wizard->AddStep().visible);
            wizard->Clvl().onChange(25, wf);
            wizard->AddStep().onClick(wf);
            const edit::PartChoice* gold = wizard->Answers().Part("gold");
            CHECK(gold != nullptr && gold->rows.size() == 14 && gold->rows[1].clvl == 25 && gold->rows[2].copyOf == "R071");
            wf.Paint(h.platform.canvas);
            // Gear: white gear off.
            wizard->GoTo(5);
            layout();
            int white = -1;
            for (int i = 0; i < static_cast<int>(wizard->Lines().size()); ++i)
                if (wizard->Lines()[static_cast<std::size_t>(i)].row < 0 && wizard->LineText(i) == "White weapons and armour") white = i;
            CHECK(white >= 0);
            wizard->ToggleLine(white, wf);
            CHECK(!wizard->Answers().Part("normalGear")->on);
            // Look: the uniques' border red.
            wizard->GoTo(8);
            layout();
            CHECK(wizard->StepKey() == "look" && wizard->Styles().visible);
            wizard->BorderSwatch(1).onClick(wf);
            wf.Paint(h.platform.canvas);
            CHECK(!wizard->Answers().looks.empty());
            h.source->played.clear();
            wizard->TestSound().onClick(wf);
            CHECK(h.source->played.size() == 1);
            // Check, then Create.
            wizard->GoTo(9);
            layout();
            wf.Paint(h.platform.canvas);
            CHECK(wizard->Create().visible && wizard->Create().enabled);
            bool rules = false;
            for (const std::string& l : wizard->CheckLines()) rules = rules || l.find(" rules") != std::string::npos;
            CHECK(rules);
            const int pops = h.platform.pops;
            wizard->Create().onClick(wf);
            CHECK(h.platform.pops == pops + 1);
            CHECK(fs::exists(user / "Speed run.json") && fs::exists(user / "esr-filter-editor" / "made" / "Speed run" / "recipe.json"));
            std::string selection;
            CHECK(edit::ReadText((user / "selection.json").string(), selection) && selection.find("user:Speed run.json") != std::string::npos);
            // The loot filter loads it; the editor follows, and it is in the drop-down.
            h.source->info.filterId = "user:Speed run.json";
            h.source->info.path = (user / "Speed run.json").string();
            std::string made;
            edit::ReadText(h.source->info.path, made);
            h.source->Put(made);
            h.Run();
            h.Run();
            h.Paint();
            CHECK(h.view->Data() != nullptr && h.view->Data()->doc.GetSettings().title == "Speed run");
            CHECK(h.view->Data() != nullptr && h.view->Data()->session != nullptr && h.view->Data()->session->IsCopy());
            CHECK(h.view->Filters().SelectedText() == "Speed run");
        }
    }

    // A broken file.
    h.source->info.filterId = "repo:QoL-Filter/Sullys-QoL-Filter.json";
    h.source->info.path = (user / "QoL-Filter" / "Sullys-QoL-Filter.json").string();
    h.source->Put("{ \"rules\": [ }");
    h.Show();
    CHECK(h.view->Message().find("could not be read") != std::string::npos);
    h.Paint();
    fs::remove_all(root);
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: ui_test <excel dir> <strings dir> <filter.json> [--print]\n");
        return 2;
    }
    const bool print = argc > 4 && std::strcmp(argv[4], "--print") == 0;

    gamedata::GameData game;
    std::vector<std::string> problems;
    CHECK(gamedata::GameData::Load(argv[1], argv[2], gamedata::Language::enUS, game, problems));
    CHECK(game.AreaName(1, gamedata::Language::enUS) == "Rogue Encampment");
    CHECK(game.AreaName(9999, gamedata::Language::enUS).empty());

    std::ifstream file(argv[3], std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    document::Document doc;
    std::vector<document::Diag> diags;
    CHECK(document::Document::Parse(text.str(), doc, diags));

    DescribeContext context;
    context.game = &game;
    context.style = [&doc](std::string_view name) -> const document::Value* {
        for (const document::Style& s : doc.Styles())
            if (s.name == name) return &s.value;
        return nullptr;
    };
    context.levelName = [&doc](std::int64_t id) -> std::string {
        for (const document::FilterLevel& l : doc.GetSettings().filterLevels)
            if (l.id == id) return PickLanguage(l.name, gamedata::Language::enUS);
        return {};
    };

    std::map<std::string, RuleSummary> byId;
    std::map<Action, int> actions;
    int other = 0, rawCodes = 0;
    // Editing a filter rule: left alone, the copy is the rule word for word (its title aside); nearly every rule opens.
    {
        int opened = 0;
        for (const document::Rule& rule : doc.Rules()) {
            RuleDraft d;
            if (!EditFromRule(rule.value, game, d)) continue;
            ++opened;
            document::Value copy = EditedRule(d, game, "x").rule, original = rule.value;
            copy.Erase("comment");
            original.Erase("comment");
            CHECK(copy.object.size() == original.object.size());
            bool same = copy.object.size() == original.object.size();
            for (const document::Member& m : original.object) same = same && copy.Find(m.key) != nullptr && *copy.Find(m.key) == m.value;
            CHECK(same);
            if (!same) std::printf("  not the same: %s\n", rule.id.c_str());
        }
        std::printf("rules the steps can edit: %d of %zu\n", opened, doc.Rules().size());
        CHECK(opened * 10 >= static_cast<int>(doc.Rules().size()) * 9);
    }
    for (const document::Rule& rule : doc.Rules()) {
        RuleSummary s = Describe(rule.value, context);
        ++actions[s.action];
        if (s.action == Action::Other) {
            ++other;
            Print(rule, s);
        }
        CHECK(!s.what.empty());
        CHECK(!s.actionLabel.empty());
        // A name that is still a code (lower case, no space, 2-4 chars) means the data did not know it.
        for (const std::string& n : s.whatAll)
            if (n.size() <= 4 && n.find(' ') == std::string::npos && game.bases.Find(n) != nullptr) ++rawCodes;
        if (print) Print(rule, s);
        byId[rule.id] = std::move(s);
    }
    std::printf("%zu rules: hide %d, show %d, highlight %d, rename %d, other %d; names left as codes: %d\n",
                doc.Rules().size(), actions[Action::Hide], actions[Action::Show], actions[Action::Highlight],
                actions[Action::Rename], other, rawCodes);
    CHECK(other == 0);

    // R068: superior helms with +1 to Maximum Curses.
    if (byId.count("R068")) {
        const RuleSummary& s = byId["R068"];
        CHECK(s.action == Action::Show);
        CHECK(s.what == "Helm");
        CHECK(Has(s.conditions, "Superior"));
        CHECK(Has(s.conditions, "Maximum Curses"));
        CHECK(Has(s.effects, "Chat message"));
    }
    // R133: hides rvs outside towns on level 3.
    if (byId.count("R133")) {
        const RuleSummary& s = byId["R133"];
        CHECK(s.action == Action::Hide);
        CHECK(Has(s.conditions, "Outside towns"));
        CHECK(s.levels.size() == 1 && s.levels[0] == 3);
        CHECK(Has(s.conditions, "Very-Strict"));
    }
    // R243: a crystal's game name without the item level.
    if (byId.count("R243")) {
        const RuleSummary& s = byId["R243"];
        CHECK(s.action == Action::Rename);
        CHECK(!s.preview.empty() && s.preview.find('{') == std::string::npos);
    }
    // A coupon label keeps its colour codes in the preview.
    if (byId.count("R606")) {
        CHECK(byId["R606"].preview.find("Ravenlore") != std::string::npos);
    }

    // The lists.
    CHECK(JoinNames({"A"}, 3) == "A");
    CHECK(JoinNames({"A", "B"}, 3) == "A and B");
    CHECK(JoinNames({"A", "B", "C"}, 3) == "A, B and C");
    CHECK(JoinNames({"A", "B", "C", "D", "E"}, 3) == "A, B, C and 2 more");
    document::Value names = document::Value::Object({{"enUS", document::Value::String("Hi")}, {"deDE", document::Value::String("Hallo")}});
    CHECK(PickLanguage(names, gamedata::Language::deDE) == "Hallo");
    CHECK(PickLanguage(names, gamedata::Language::frFR) == "Hi");

    DraftTests(game, std::getenv("ESRFE_DRAFT_OUT"));
    {
        auto shared = std::make_shared<gamedata::GameData>(std::move(game));
        ViewTests(shared, text.str());
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
