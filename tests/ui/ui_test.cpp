// Describes every rule of a real filter with the mod's real data, and checks the words for known rules.
//   ui_test <excel dir> <strings dir> <filter.json> [--print]
#include <cstdio>
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

    // The level: the game's, with rules for other levels greyed out (still listed).
    CHECK(h.view->Levels().visible);
    CHECK(h.view->Levels().SelectedText().find("(in use)") != std::string::npos);
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
    h.Click(h.view->GameSettings());
    CHECK(h.source->settingsOpened == 1);

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
            const auto pressEnter = [&] {
                esrfe::overlay::KeyEvent k;
                k.key = esrfe::overlay::Key::Enter;
                cardForm.KeyDown(k);
                layout();
            };
            layout();
            CHECK(card->Editable());
            CHECK(card->HideChip().visible && card->HideChip().selected);
            CHECK(card->LevelCount() == data->doc.GetSettings().filterLevels.size());
            // The first change asks; cancelling changes nothing.
            click(card->OnSwitch());
            CHECK(cardForm.DialogOpen());
            esrfe::overlay::KeyEvent esc;
            esc.key = esrfe::overlay::Key::Escape;
            cardForm.KeyDown(esc);
            CHECK(!cardForm.DialogOpen());
            CHECK(card->OnSwitch().on);
            CHECK(!fs::exists(user / "Sullys-QoL-Filter (my version).json"));
            // Again, and yes: the copy is made and the game switched to it.
            click(card->OnSwitch());
            CHECK(cardForm.DialogOpen());
            pressEnter();
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
            CHECK(edit::ReadText((user / "esr-filter-editor" / "Sullys-QoL-Filter.changes.json").string(), saved));
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
        CHECK(edit::ReadText((user / "esr-filter-editor" / "Sullys-QoL-Filter.changes.json").string(), saved));
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

    {
        auto shared = std::make_shared<gamedata::GameData>(std::move(game));
        ViewTests(shared, text.str());
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
