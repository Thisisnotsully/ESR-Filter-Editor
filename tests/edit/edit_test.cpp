// The changes model and the merge, on Sully's QoL Filter.   edit_test <filter.json>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <algorithm>
#include <filesystem>

#include "changes.h"
#include "session.h"
#include "document.h"

using namespace esrfe::edit;
using document::Value;

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

static auto Read(const std::string& text, document::Document& doc) -> bool {
    std::vector<document::Diag> diags;
    const bool ok = document::Document::Parse(text, doc, diags);
    if (!ok && !diags.empty()) std::printf("  parse: line %u %s\n", diags.front().line, diags.front().message.c_str());
    return ok;
}

static auto RuleOf(const document::Document& doc, const std::string& id) -> const Value* {
    const int i = doc.FindRule(id);
    return i < 0 ? nullptr : &doc.Rules()[static_cast<std::size_t>(i)].value;
}

static auto Ints(std::initializer_list<int> v) -> Value {
    Value a = Value::Array();
    for (int i : v) a.array.push_back(Value::Integer(i));
    return a;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string author = buffer.str();
    document::Document base;
    CHECK(Read(author, base));
    const Value r090 = *RuleOf(base, "R099");
    const Value r125 = *RuleOf(base, "R133");

    // Nothing changed: the copy is the author's filter with its own title and no update address.
    ChangeSet changes;
    changes.base = "repo:QoL-Filter/Sullys-QoL-Filter.json";
    changes.merged = "user:Sullys-QoL-Filter (my version).json";
    MergeResult m = Merge(author, changes);
    CHECK(m.ok);
    document::Document copy;
    CHECK(Read(m.text, copy));
    CHECK(copy.GetSettings().title == base.GetSettings().title + " (my version)");
    CHECK(copy.GetSettings().updateUrl.empty());
    CHECK(copy.ActiveCount() == base.ActiveCount());
    CHECK(m.conflicts.empty());
    // Merging the copy's title again does not add the suffix twice.
    CHECK(Merge(m.text, changes).text == m.text);

    // Editing: levels, the action, an item, a rule off.
    RuleChange& c090 = changes.Get("R099");
    Value levels = Ints({3, 4});
    SetField(c090, r090, "activeInLevels", &levels);
    CHECK(c090.fields.size() == 1 && c090.fields[0].authorHad);
    CHECK(Effective(r090, c090).Find("activeInLevels")->array.size() == 2);
    Value back = *r090.Find("activeInLevels");
    SetField(c090, r090, "activeInLevels", &back);  // back to the author's: no edit
    CHECK(c090.fields.empty());
    SetField(c090, r090, "activeInLevels", &levels);
    const Value gem = r090.Find("itemCodes")->array[1];
    SetItem(c090, r090, "itemCodes", gem, true);
    CHECK(c090.RemovedFrom("itemCodes") != nullptr && c090.RemovedFrom("itemCodes")->size() == 1);
    CHECK(Effective(r090, c090).Find("itemCodes")->array.size() == r090.Find("itemCodes")->array.size() - 1);

    RuleChange& c125 = changes.Get("R133");  // hide -> always show
    SetField(c125, r125, "hide", nullptr);
    Value yes = Value::Bool(true);
    SetField(c125, r125, "show", &yes);
    CHECK(c125.fields.size() == 2);
    CHECK(Effective(r125, c125).Find("hide") == nullptr && Effective(r125, c125).Find("show") != nullptr);
    changes.Get("R303").off = true;
    CHECK(!changes.rules[0].snapshot.IsNull());

    m = Merge(author, changes);
    CHECK(m.ok);
    CHECK(Read(m.text, copy));
    CHECK(copy.ActiveCount() == base.ActiveCount() - 1);
    CHECK(RuleOf(copy, "R303") == nullptr);
    CHECK(*RuleOf(copy, "R099")->Find("activeInLevels") == levels);
    CHECK(RuleOf(copy, "R099")->Find("itemCodes")->array.size() == r090.Find("itemCodes")->array.size() - 1);
    CHECK(RuleOf(copy, "R133")->Find("show") != nullptr && RuleOf(copy, "R133")->Find("hide") == nullptr);
    // Only the changed rules' text changed.
    {
        std::size_t same = 0;
        for (const document::Rule& r : copy.Rules())
            if (const Value* a = RuleOf(base, r.id); a != nullptr && *a == r.value) ++same;
        CHECK(static_cast<int>(same) == base.ActiveCount() - 3);
    }

    // The player's own rules.
    {
        OwnRule own;
        own.id = changes.NextOwnId();
        CHECK(own.id == "U001");
        own.after = "R303";  // off, but still a place to follow
        own.section = "Highlights and Labels > Special items";
        own.rule = Value::Object({{"comment", Value::String("U001 | my rule | hide")}, {"itemCodes", Value::Array({Value::String("gcv")})}, {"hide", yes}});
        changes.own.push_back(own);
        OwnRule lost = own;
        lost.id = changes.NextOwnId();
        CHECK(lost.id == "U002");
        lost.after = "R999";
        lost.section = "Nowhere > At all";
        lost.rule.Set("comment", Value::String("U002 | lost | hide"));
        changes.own.push_back(lost);
        m = Merge(author, changes);
        CHECK(m.ok);
        CHECK(Read(m.text, copy));
        CHECK(RuleOf(copy, "U001") != nullptr);
        const int u2 = copy.FindRule("U002");
        CHECK(u2 >= 0 && copy.Rules()[static_cast<std::size_t>(u2)].part == copy.FindHeading("My Rules"));
        CHECK(m.notes.size() == 1);
        // U001 sits where R303 was (R303 is off and gone).
        const int u1 = copy.FindRule("U001");
        const int r270 = copy.FindRule("R302");
        {
            const auto sectionName = [](const document::Document& d, int rule) -> std::string {
                const int h = rule < 0 ? -1 : d.Rules()[static_cast<std::size_t>(rule)].section;
                return h < 0 ? std::string{} : d.Headings()[static_cast<std::size_t>(h)].name;
            };
            CHECK(u1 >= 0 && sectionName(copy, u1) == sectionName(base, base.FindRule("R303")));
        }
        (void)r270;
    }

    // The file round-trips.
    changes.baseHash = Hash(author);
    changes.baseVersion = base.GetSettings().version;
    const std::string saved = Render(changes);
    ChangeSet again;
    std::string error;
    CHECK(Parse(saved, again, error));
    CHECK(Render(again) == saved);
    CHECK(again.rules.size() == 3 && again.own.size() == 2 && again.baseHash == changes.baseHash);

    // The author updates: R099's levels changed by the author too, R133 deleted, R303 untouched.
    {
        document::Document updated;
        CHECK(Read(author, updated));
        CHECK(updated.SetField(updated.FindRule("R099"), "activeInLevels", Ints({2, 4})));
        CHECK(updated.DeleteRule(updated.FindRule("R133")));
        CHECK(updated.SetSetting("version", Value::String("9.9.9")));
        const std::string newer = updated.Text();
        m = Merge(newer, changes);
        CHECK(m.ok);
        int fieldConflicts = 0, deleted = 0;
        for (const Conflict& c : m.conflicts) {
            if (c.kind == Conflict::Kind::Field && c.id == "R099" && c.key == "activeInLevels") ++fieldConflicts;
            if (c.kind == Conflict::Kind::Deleted && c.id == "R133") ++deleted;
        }
        CHECK(fieldConflicts == 1 && deleted == 1 && m.conflicts.size() == 2);
        // Meanwhile the copy has the player's levels.
        CHECK(Read(m.text, copy));
        CHECK(*RuleOf(copy, "R099")->Find("activeInLevels") == levels);

        // Keep mine for both: R099 keeps [3, 4] and follows the author's [2, 4] from now on; R133 becomes U003.
        ChangeSet keep = changes;
        for (const Conflict& c : m.conflicts) Resolve(keep, c, true);
        keep.baseHash = Hash(newer);
        CHECK(keep.Find("R133") == nullptr);
        CHECK(keep.own.size() == 3 && keep.own.back().id == "U003");
        CHECK(keep.own.back().rule.Find("comment")->Str().rfind("U003 | On level 3", 0) == 0);
        CHECK(keep.own.back().rule.Find("show") != nullptr);  // with the player's change
        m = Merge(newer, keep);
        CHECK(m.ok && m.conflicts.empty());
        CHECK(Read(m.text, copy));
        CHECK(*RuleOf(copy, "R099")->Find("activeInLevels") == levels);
        CHECK(RuleOf(copy, "U003") != nullptr);
        if (argc > 2) {  // for the loot filter's own reader (tools/filtercheck)
            std::ofstream out(argv[2], std::ios::binary);
            out << m.text;
        }

        // Use the author's for both.
        ChangeSet theirs = changes;
        for (const Conflict& c : Merge(newer, changes).conflicts) Resolve(theirs, c, false);
        theirs.baseHash = Hash(newer);
        CHECK(theirs.Find("R133") == nullptr && theirs.own.size() == 2);
        CHECK(theirs.Find("R099") != nullptr && theirs.Find("R099")->fields.empty());  // the removed gem stays
        m = Merge(newer, theirs);
        CHECK(m.ok && m.conflicts.empty());
        CHECK(Read(m.text, copy));
        CHECK(*RuleOf(copy, "R099")->Find("activeInLevels") == Ints({2, 4}));
    }

    // The game's selection.
    {
        std::string out;
        const std::string selection =
            "{\n  \"formatVersion\": 1,\n  \"activeFilter\": \"repo:QoL-Filter/Sullys-QoL-Filter.json\",\n"
            "  \"levels\": {\"repo:QoL-Filter/Sullys-QoL-Filter.json\": 3},\n  \"sounds\": true,\n  \"chatNotifications\": false\n}\n";
        CHECK(SwitchSelection(selection, "repo:QoL-Filter/Sullys-QoL-Filter.json", "user:Sullys-QoL-Filter (my version).json", out));
        Value v;
        document::Syntax syntax;
        document::Diag diag;
        CHECK(document::ParseSyntax(out, syntax, diag));
        v = document::ToValue(syntax, out, syntax.root);
        CHECK(v.Find("activeFilter")->Str() == "user:Sullys-QoL-Filter (my version).json");
        CHECK(v.Find("levels")->Find("user:Sullys-QoL-Filter (my version).json") != nullptr);
        CHECK(v.Find("chatNotifications") != nullptr && !v.Find("chatNotifications")->boolean);
        CHECK(SwitchSelection("", "a", "user:x.json", out) && out.find("user:x.json") != std::string::npos);
        CHECK(!SwitchSelection("[1]", "a", "b", out));
    }

    // ---- own rules: "top" ones beat the author's, "end" highlights keep their colours and are never hidden ----
    {
        ChangeSet own;
        own.base = "repo:QoL-Filter/Sullys-QoL-Filter.json";
        OwnRule hide;
        hide.id = "U001";
        hide.place = "top";
        hide.rule = Value::Object({{"comment", Value::String("U001 | superior caps | hide")}, {"itemCodes", Value::Array({Value::String("cap")})},
                                   {"quality", Value::String("superior")}, {"hide", Value::Bool(true)}});
        own.own.push_back(hide);
        OwnRule mark;
        mark.id = "U002";
        mark.place = "end";
        mark.keepShown = true;
        mark.rule = Value::Object({{"comment", Value::String("U002 | superior light crossbows | highlight")},
                                   {"itemCodes", Value::Array({Value::String("lxb")})}, {"quality", Value::String("superior")},
                                   {"labelBorder", Value::Object({{"color", Value::Array({Value::Integer(255), Value::Integer(0), Value::Integer(0), Value::Integer(255)})},
                                                                  {"width", Value::Integer(2)}})}});
        own.own.push_back(mark);
        MergeResult m2 = Merge(author, own);
        CHECK(m2.ok);
        document::Document merged;
        CHECK(Read(m2.text, merged));
        CHECK(merged.FindHeading("My Rules") == 0);
        CHECK(merged.FindHeading("My Highlights") == static_cast<int>(merged.Headings().size()) - 1);
        CHECK(merged.FindRule("U001") >= 0 && merged.FindRule("U001") < 2);
        // U002's show-only copy first, the highlight itself last.
        const int companion = merged.FindRule("U002");
        CHECK(companion >= 0 && companion < 2 && merged.Rules()[static_cast<std::size_t>(companion)].value.Find("show") != nullptr);
        CHECK(merged.Rules()[static_cast<std::size_t>(merged.ActiveCount() - 1)].value.Find("labelBorder") != nullptr);
        // The player's hide comes after their shows, even one added before them.
        CHECK(companion == 0 && merged.FindRule("U001") == 1);
        {
            ChangeSet order = own;
            OwnRule show;
            show.id = "U003";
            show.place = "top";
            show.rule = Value::Object({{"comment", Value::String("U003 | superior caps | show")}, {"itemCodes", Value::Array({Value::String("cap")})},
                                       {"show", Value::Bool(true)}});
            order.own.push_back(show);
            document::Document ordered;
            CHECK(Read(Merge(author, order).text, ordered));
            CHECK(ordered.FindRule("U003") == 1 && ordered.FindRule("U001") == 2);
        }
        // The editor's own view has no copy.
        MergeOptions view;
        view.companions = false;
        document::Document seen;
        CHECK(Read(Merge(author, own, view).text, seen));
        CHECK(seen.ActiveCount() == base.ActiveCount() + 2);
        if (argc > 3) {  // for the loot filter's engine (tests/edit/own-rules.txt)
            std::ofstream out(argv[3], std::ios::binary);
            out << m2.text;
        }
        // Off: left out of the game's copy, kept in the editor's view.
        own.own[0].off = true;
        CHECK(Read(Merge(author, own).text, merged));
        CHECK(merged.FindRule("U001") < 0);
        // The file keeps all of it.
        ChangeSet back;
        std::string why;
        CHECK(Parse(Render(own), back, why));
        CHECK(back.own.size() == 2 && back.own[0].off && back.own[1].place == "end" && back.own[1].keepShown);
    }

    // ---- a session on real folders ----
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / "esrfe-session-test";
        fs::remove_all(root);
        const fs::path user = root / "LootFilters";
        const fs::path repo = user / "QoL-Filter";
        fs::create_directories(repo / "sounds");
        fs::create_directories(repo / "icons");
        fs::create_directories(user / "sounds");
        CHECK(WriteText((repo / "Sullys-QoL-Filter.json").string(), author));
        CHECK(WriteText((repo / "sounds" / "gong.mp3").string(), "GONG"));
        CHECK(WriteText((repo / "icons" / "gem.sprite").string(), "GEM"));
        CHECK(WriteText((user / "sounds" / "tink.mp3").string(), "SOMEONE ELSE'S"));  // not the editor's: left alone
        CHECK(WriteText((repo / "sounds" / "tink.mp3").string(), "TINK"));
        const std::string repoId = "repo:QoL-Filter/Sullys-QoL-Filter.json";
        CHECK(WriteText((user / "selection.json").string(),
                        "{\n  \"formatVersion\": 1,\n  \"activeFilter\": \"" + repoId + "\",\n  \"levels\": {\"" + repoId + "\": 2}\n}\n"));
        Folders folders{user.string(), (root / "shipped").string()};
        CHECK(PathOfId(repoId, folders) == (repo / "Sullys-QoL-Filter.json").string());
        CHECK(PathOfId("repo:../x.json", folders).empty());
        CHECK(StemOfId("user:Sullys-QoL-Filter (my version).json") == "Sullys-QoL-Filter");

        Session session;
        std::string error, text0;
        CHECK(session.Open(repoId, PathOfId(repoId, folders), folders, error));
        CHECK(!session.IsCopy() && !session.HasChanges());
        CHECK(session.NeedsName() && session.MergedId().empty());
        session.SetOff("R303", true);
        session.SetItem("R099", "itemCodes", gem, true);
        CHECK(session.IsOff("R303"));
        CHECK(session.Effective("R099").Find("itemCodes")->array.size() == r090.Find("itemCodes")->array.size() - 1);
        document::Document view;
        CHECK(Read(session.ViewText(), view));
        CHECK(view.FindRule("R303") >= 0);  // off rules stay in what the editor shows
        bool switched = false;
        CHECK(!session.Save(error, switched));  // no name yet
        // The name: a file name, not used yet.
        CHECK(SuggestVersionName(folders, "Sully's QoL Filter") == "Sully's QoL Filter (my version)");
        for (const char* bad : {"", " x", "x.", "a/b", "a:b", "a..b", "selection", "a?b"}) CHECK(!CheckVersionName(folders, bad, error));
        CHECK(!session.NameVersion("a|b", error) && session.NeedsName());
        CHECK(session.NameVersion("Sullys-QoL-Filter (my version)", error));
        CHECK(!session.NeedsName() && session.MergedId() == "user:Sullys-QoL-Filter (my version).json");
        CHECK(session.Save(error, switched));
        if (!error.empty()) std::printf("  save: %s\n", error.c_str());
        CHECK(!CheckVersionName(folders, "Sullys-QoL-Filter (my version)", error));  // taken now
        CHECK(ReadText((user / "esr-filter-editor" / "Sullys-QoL-Filter (my version).changes.json").string(), text0));
        CHECK(switched && session.IsCopy());
        std::string text;
        CHECK(ReadText((user / "selection.json").string(), text));
        CHECK(text.find("\"activeFilter\": \"user:Sullys-QoL-Filter (my version).json\"") != std::string::npos);
        CHECK(text.find("\"user:Sullys-QoL-Filter (my version).json\": 2") != std::string::npos);
        CHECK(ReadText(session.MergedPath(), text));
        CHECK(Read(text, copy));
        CHECK(copy.FindRule("R303") < 0 && copy.ActiveCount() == base.ActiveCount() - 1);
        CHECK(copy.GetSettings().title == "Sullys-QoL-Filter (my version)");  // its name
        CHECK(ReadText((user / "sounds" / "gong.mp3").string(), text) && text == "GONG");
        CHECK(ReadText((user / "icons" / "gem.sprite").string(), text) && text == "GEM");
        CHECK(ReadText((user / "sounds" / "tink.mp3").string(), text) && text == "SOMEONE ELSE'S");
        CHECK(!fs::exists(user / "selection.json.tmp"));

        // The sounds the author's filter has, and the volume the player sets.
        CHECK(session.SoundFiles() == (std::vector<std::string>{"gong.mp3", "tink.mp3"}));
        CHECK(session.SoundPath("gong.mp3") == (repo / "sounds" / "gong.mp3").string());
        CHECK(session.SoundVolume() == base.GetSettings().soundVolume.value_or(100));
        Value forty = Value::Integer(40);
        session.SetSetting("soundVolume", &forty);
        CHECK(session.SoundVolume() == 40);
        CHECK(session.Save(error, switched));
        CHECK(ReadText(session.MergedPath(), text) && Read(text, copy) && copy.GetSettings().soundVolume == 40);

        // Opening the copy again: the changes come back, nothing switches.
        Session again;
        CHECK(again.Open(session.MergedId(), session.MergedPath(), folders, error));
        CHECK(again.IsCopy() && again.IsOff("R303") && again.Conflicts().empty());
        again.SetOff("R303", false);
        CHECK(!again.IsOff("R303"));
        CHECK(again.Save(error, switched) && !switched);

        // The player went back to Sully's filter: a change there makes another version, the first one stays as it is.
        Session back;
        CHECK(back.Open(repoId, PathOfId(repoId, folders), folders, error));
        CHECK(!back.IsCopy() && !back.HasChanges() && back.NeedsName() && !back.IsOff("R303"));
        CHECK(VersionsOf(folders, repoId).size() == 1 && VersionsOf(folders, repoId)[0].id == session.MergedId());
        CHECK(SuggestVersionName(folders, "Sullys-QoL-Filter") == "Sullys-QoL-Filter (my version 2)");
        back.SetOff("R001", true);
        CHECK(back.NameVersion("Rune farming", error));
        CHECK(back.Save(error, switched) && switched && back.IsCopy());
        CHECK(VersionsOf(folders, repoId).size() == 2);
        {
            Session first;
            CHECK(first.Open(session.MergedId(), session.MergedPath(), folders, error));
            CHECK(first.IsCopy() && !first.IsOff("R001"));  // untouched
            Session second;
            CHECK(second.Open("user:Rune farming.json", (user / "Rune farming.json").string(), folders, error));
            CHECK(second.IsCopy() && second.IsOff("R001") && !second.IsOff("R303"));
            std::string t;
            document::Document d;
            CHECK(ReadText((user / "Rune farming.json").string(), t) && Read(t, d) && d.GetSettings().title == "Rune farming");
            const std::vector<FilterChoice> choices = ListFilters(folders);
            int mine = 0;
            for (const FilterChoice& c : choices) mine += c.mine ? 1 : 0;
            CHECK(mine == 2);
        }
        CHECK(SelectFilter(folders, session.MergedId(), error));

        // Sully updates: R099's levels change (the player did not change them), the copy follows at the next start.
        {
            document::Document updated;
            CHECK(Read(author, updated));
            CHECK(updated.SetField(updated.FindRule("R099"), "activeInLevels", Ints({1, 4})));
            CHECK(WriteText((repo / "Sullys-QoL-Filter.json").string(), updated.Text()));
            const auto updates = UpdateCopies(folders);
            CHECK(updates.size() == 2);  // both versions follow
            for (const auto& u : updates) CHECK(u.updated && u.conflicts == 0);
            CHECK(ReadText(session.MergedPath(), text) && Read(text, copy));
            CHECK(*RuleOf(copy, "R099")->Find("activeInLevels") == Ints({1, 4}));
            CHECK(RuleOf(copy, "R099")->Find("itemCodes")->array.size() == r090.Find("itemCodes")->array.size() - 1);
            CHECK(UpdateCopies(folders).empty());  // nothing new
        }
        // Another update that conflicts: the player changed R133's action, Sully too.
        {
            Session s2;
            CHECK(s2.Open(session.MergedId(), session.MergedPath(), folders, error));
            s2.SetField("R133", "hide", nullptr);
            CHECK(s2.Save(error, switched));
            std::string current;
            CHECK(ReadText((repo / "Sullys-QoL-Filter.json").string(), current));
            document::Document updated;
            CHECK(Read(current, updated));
            CHECK(updated.RemoveField(updated.FindRule("R133"), "hide"));
            CHECK(updated.SetField(updated.FindRule("R133"), "show", Value::Bool(true)));
            CHECK(WriteText((repo / "Sullys-QoL-Filter.json").string(), updated.Text()));
            const auto updates = UpdateCopies(folders);
            // The player removed "hide" and so did Sully: the same, no conflict.
            CHECK(updates.size() == 2);
            for (const auto& u : updates) CHECK(u.conflicts == 0);
        }
        // The filters to choose from: the repository's (it has .esr-repo.json), the player's version, a shipped one;
        // not the editor's changes, not a JSON file that is no filter, not a folder that is no repository.
        {
            CHECK(WriteText((repo / ".esr-repo.json").string(), "{}"));
            fs::create_directories(root / "shipped");
            CHECK(WriteText((root / "shipped" / "sources.json").string(), "{\"sources\": []}"));
            CHECK(WriteText((root / "shipped" / "Plain.json").string(), "{\"formatVersion\": 1, \"rules\": []}"));
            fs::create_directories(user / "loose");
            CHECK(WriteText((user / "loose" / "Other.json").string(), "{\"title\": \"Loose\", \"rules\": []}"));
            const std::vector<FilterChoice> filters = ListFilters(folders);
            std::vector<std::string> ids;
            for (const FilterChoice& f : filters) ids.push_back(f.id);
            CHECK(ids.size() == 4);  // both versions
            CHECK(std::find(ids.begin(), ids.end(), repoId) != ids.end());
            CHECK(std::find(ids.begin(), ids.end(), "user:Sullys-QoL-Filter (my version).json") != ids.end());
            CHECK(std::find(ids.begin(), ids.end(), "shipped:Plain.json") != ids.end());
            for (const FilterChoice& f : filters) {
                if (f.id == repoId) CHECK(f.title == "Sully's QoL Filter" && !f.mine);
                if (f.id.starts_with("user:")) CHECK(f.mine);
                if (f.id == "user:Rune farming.json") CHECK(f.title == "Rune farming");
                if (f.id == "shipped:Plain.json") CHECK(f.title == "Plain");
            }
            std::string error, selection;
            CHECK(SelectFilter(folders, repoId, error));
            CHECK(ReadText((user / "selection.json").string(), selection));
            CHECK(selection.find("\"activeFilter\": \"" + repoId + "\"") != std::string::npos);
            CHECK(selection.find("\"levels\"") != std::string::npos);  // the rest kept
            CHECK(!SelectFilter(folders, "user:../x.json", error));
        }
        fs::remove_all(root);
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
