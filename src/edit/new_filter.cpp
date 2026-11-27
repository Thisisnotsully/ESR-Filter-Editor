#include "new_filter.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>

#include "changes.h"
#include "syntax.h"

namespace esrfe::edit {

namespace fs = std::filesystem;
using document::Member;
using document::Value;

namespace {

auto P(const std::string& utf8) -> fs::path { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
auto U(const fs::path& path) -> std::string {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

auto ReadValue(std::string_view text, Value& out, std::string& error) -> bool {
    document::Syntax syntax;
    document::Diag diag;
    if (!document::ParseSyntax(text, syntax, diag)) {
        error = "line " + std::to_string(diag.line) + ": " + diag.message;
        return false;
    }
    out = document::ToValue(syntax, text, syntax.root);
    return true;
}

auto Str(const Value* v) -> std::string { return v != nullptr && v->IsString() ? v->Str() : std::string{}; }
auto Int(const Value* v, std::int64_t fallback) -> std::int64_t {
    std::int64_t n = fallback;
    if (v != nullptr) v->AsInteger(n);
    return n;
}
auto Bool(const Value* v, bool fallback) -> bool { return v != nullptr && v->IsBool() ? v->boolean : fallback; }

auto Rgb(const Value* v) -> std::optional<Rgb3> {
    if (v == nullptr || !v->IsArray() || v->array.size() < 3) return std::nullopt;
    Rgb3 c{};
    for (std::size_t i = 0; i < 3; ++i) c[i] = static_cast<int>(Int(&v->array[i], 0));
    return c;
}
auto RgbValue(const Rgb3& c, std::optional<std::int64_t> alpha) -> Value {
    std::vector<Value> items{Value::Integer(c[0]), Value::Integer(c[1]), Value::Integer(c[2])};
    if (alpha) items.push_back(Value::Integer(*alpha));
    return Value::Array(std::move(items));
}

// The level a rule's "playerStats" asks for ("level" >= n), 0 when none.
auto CharacterLevel(const Value& rule) -> int {
    const Value* stats = rule.Find("playerStats");
    if (stats == nullptr || !stats->IsArray()) return 0;
    for (const Value& s : stats->array)
        if (Str(s.Find("stat")) == "level" && Str(s.Find("op")) == ">=") return static_cast<int>(Int(s.Find("value"), 0));
    return 0;
}

// The gold amount of a gold step (itemStats gold <= n), -1 when none.
auto GoldAmount(const Value& rule) -> std::int64_t {
    const Value* stats = rule.Find("itemStats");
    if (stats == nullptr || !stats->IsArray()) return -1;
    for (const Value& s : stats->array)
        if (Str(s.Find("stat")) == "gold" && Str(s.Find("op")) == "<=") return Int(s.Find("value"), -1);
    return -1;
}

}  // namespace

auto Starter::Part(const std::string& key) const -> const StarterPart* {
    for (const StarterPart& p : parts)
        if (p.key == key) return &p;
    return nullptr;
}

auto Recipe::Part(const std::string& key) const -> const PartChoice* {
    for (const PartChoice& p : parts)
        if (p.key == key) return &p;
    return nullptr;
}
auto Recipe::Part(const std::string& key) -> PartChoice* {
    for (PartChoice& p : parts)
        if (p.key == key) return &p;
    return nullptr;
}
auto Recipe::Look(const std::string& style) const -> const LookChoice* {
    for (const LookChoice& l : looks)
        if (l.style == style) return &l;
    return nullptr;
}
auto Recipe::Look(const std::string& style) -> LookChoice& {
    for (LookChoice& l : looks)
        if (l.style == style) return l;
    looks.push_back(LookChoice{style, {}, {}, {}});
    return looks.back();
}
auto Recipe::NextAddedId() const -> std::string {
    int top = 0;
    for (const PartChoice& p : parts)
        for (const Row& r : p.rows)
            if (r.id.size() == 4 && r.id[0] == 'W') top = std::max(top, std::atoi(r.id.c_str() + 1));
    std::string id = std::to_string(top + 1);
    while (id.size() < 3) id.insert(id.begin(), '0');
    return "W" + id;
}

auto LoadStarter(const std::string& folder, Starter& out, std::string& error) -> bool {
    out = Starter{};
    out.folder = folder;
    std::string partsText;
    if (!ReadText(U(P(folder) / "parts.json"), partsText) || !ReadText(U(P(folder) / "starter-filter.json"), out.filterText)) {
        error = "the new-filter starter is missing from " + folder;
        return false;
    }
    std::vector<document::Diag> diags;
    if (!document::Document::Parse(out.filterText, out.filter, diags)) {
        error = "the starter filter could not be read: " + (diags.empty() ? std::string{} : diags.front().message);
        return false;
    }
    Value root;
    if (!ReadValue(partsText, root, error) || !root.IsObject()) {
        error = "parts.json could not be read: " + error;
        return false;
    }
    out.from = Str(root.Find("from"));
    if (const Value* towns = root.Find("towns"); towns != nullptr && towns->IsArray())
        for (const Value& t : towns->array) out.towns.push_back(Int(&t, 0));
    if (const Value* steps = root.Find("steps"); steps != nullptr && steps->IsArray())
        for (const Value& s : steps->array) out.steps.push_back({Str(s.Find("key")), Str(s.Find("title")), Str(s.Find("help"))});
    if (const Value* parts = root.Find("parts"); parts != nullptr && parts->IsArray()) {
        for (const Value& p : parts->array) {
            StarterPart part;
            part.key = Str(p.Find("key"));
            part.step = Str(p.Find("step"));
            const std::string kind = Str(p.Find("kind"));
            part.kind = kind == "show" ? StarterPart::Kind::Show : kind == "effect" ? StarterPart::Kind::Effect : StarterPart::Kind::Hide;
            part.label = Str(p.Find("label"));
            part.help = Str(p.Find("help"));
            part.rows = Bool(p.Find("rows"), true);
            part.minimal = Bool(p.Find("minimal"), false);
            if (const Value* rules = p.Find("rules"); rules != nullptr && rules->IsArray())
                for (const Value& r : rules->array)
                    if (out.filter.FindRule(r.Str()) >= 0) part.rules.push_back(r.Str());
            out.parts.push_back(std::move(part));
        }
    }
    if (const Value* looks = root.Find("looks"); looks != nullptr && looks->IsArray())
        for (const Value& l : looks->array) out.looks.push_back({Str(l.Find("style")), Str(l.Find("label"))});
    if (out.parts.empty() || out.filter.GetSettings().filterLevels.empty()) {
        error = "the new-filter starter has no parts or no levels";
        return false;
    }
    return true;
}

auto RowOf(const Starter& starter, const std::string& id) -> Row {
    Row row;
    row.id = id;
    const int index = starter.filter.FindRule(id);
    if (index < 0) return row;
    const Value& rule = starter.filter.Rules()[static_cast<std::size_t>(index)].value;
    const int levels = starter.Levels();
    if (const Value* active = rule.Find("activeInLevels"); active != nullptr && (active->IsNumber() || (active->IsArray() && !active->array.empty()))) {
        int lo = levels, hi = 1;
        const std::vector<Value> list = active->IsArray() ? active->array : std::vector<Value>{*active};  // a single level: a number
        for (const Value& l : list) {
            const int n = static_cast<int>(Int(&l, 1));
            lo = std::min(lo, n);
            hi = std::max(hi, n);
        }
        row.from = lo;
        row.to = hi >= levels ? 0 : hi;
    }
    row.clvl = CharacterLevel(rule);
    row.amount = GoldAmount(rule);
    return row;
}

auto StarterLookOf(const Starter& starter, const std::string& style) -> LookChoice {
    LookChoice look;
    look.style = style;
    for (const document::Style& s : starter.filter.Styles()) {
        if (s.name != style) continue;
        if (const Value* border = s.value.Find("labelBorder"); border != nullptr) look.border = Rgb(border->Find("color"));
        look.background = Rgb(s.value.Find("labelBackground"));
        if (const Value* light = s.value.Find("droplight"); light != nullptr) look.beam = Rgb(light->Find("color"));
    }
    return look;
}

auto DefaultRecipe(const Starter& starter, bool minimal) -> Recipe {
    Recipe r;
    r.minimal = minimal;
    const document::Settings& s = starter.filter.GetSettings();
    for (const document::FilterLevel& l : s.filterLevels) {
        const Value* en = l.name.IsObject() ? l.name.Find("enUS") : &l.name;
        r.levels.push_back(Str(en));
    }
    r.defaultLevel = static_cast<int>(s.defaultLevel.value_or(1));
    r.soundVolume = static_cast<int>(s.soundVolume.value_or(25));
    for (const StarterPart& p : starter.parts) {
        PartChoice c;
        c.key = p.key;
        c.on = !minimal || p.minimal;
        if (p.rows)
            for (const std::string& id : p.rules) c.rows.push_back(RowOf(starter, id));
        r.parts.push_back(std::move(c));
    }
    return r;
}

// ---- the answers on disk ----

auto ParseRecipe(std::string_view text, Recipe& out, std::string& error) -> bool {
    out = Recipe{};
    Value root;
    if (!ReadValue(text, root, error)) return false;
    if (!root.IsObject()) {
        error = "not a JSON object";
        return false;
    }
    out.name = Str(root.Find("name"));
    out.author = Str(root.Find("author"));
    out.minimal = Bool(root.Find("minimal"), false);
    if (const Value* levels = root.Find("levels"); levels != nullptr && levels->IsArray())
        for (const Value& l : levels->array) out.levels.push_back(Str(&l));
    out.defaultLevel = static_cast<int>(Int(root.Find("defaultLevel"), 1));
    out.soundVolume = static_cast<int>(Int(root.Find("soundVolume"), 25));
    if (const Value* parts = root.Find("parts"); parts != nullptr && parts->IsArray()) {
        for (const Value& p : parts->array) {
            PartChoice c;
            c.key = Str(p.Find("key"));
            c.on = Bool(p.Find("on"), true);
            if (const Value* rows = p.Find("rows"); rows != nullptr && rows->IsArray()) {
                for (const Value& w : rows->array) {
                    Row row;
                    row.id = Str(w.Find("id"));
                    row.copyOf = Str(w.Find("copyOf"));
                    row.on = Bool(w.Find("on"), true);
                    row.from = static_cast<int>(Int(w.Find("from"), 1));
                    row.to = static_cast<int>(Int(w.Find("to"), 0));
                    row.clvl = static_cast<int>(Int(w.Find("clvl"), 0));
                    row.amount = Int(w.Find("amount"), -1);
                    c.rows.push_back(std::move(row));
                }
            }
            out.parts.push_back(std::move(c));
        }
    }
    if (const Value* looks = root.Find("looks"); looks != nullptr && looks->IsArray())
        for (const Value& l : looks->array)
            out.looks.push_back({Str(l.Find("style")), Rgb(l.Find("border")), Rgb(l.Find("background")), Rgb(l.Find("beam"))});
    if (out.name.empty() || out.levels.empty()) {
        error = "the answers have no name or no levels";
        return false;
    }
    return true;
}

auto RenderRecipe(const Recipe& r) -> std::string {
    std::vector<Value> levels;
    for (const std::string& l : r.levels) levels.push_back(Value::String(l));
    std::vector<Value> parts;
    for (const PartChoice& p : r.parts) {
        std::vector<Value> rows;
        for (const Row& w : p.rows) {
            std::vector<Member> m{{"id", Value::String(w.id)}};
            if (!w.copyOf.empty()) m.push_back({"copyOf", Value::String(w.copyOf)});
            m.push_back({"on", Value::Bool(w.on)});
            m.push_back({"from", Value::Integer(w.from)});
            m.push_back({"to", Value::Integer(w.to)});
            m.push_back({"clvl", Value::Integer(w.clvl)});
            if (w.amount >= 0) m.push_back({"amount", Value::Integer(w.amount)});
            rows.push_back(Value::Object(std::move(m)));
        }
        std::vector<Member> m{{"key", Value::String(p.key)}, {"on", Value::Bool(p.on)}};
        if (!rows.empty()) m.push_back({"rows", Value::Array(std::move(rows))});
        parts.push_back(Value::Object(std::move(m)));
    }
    std::vector<Value> looks;
    for (const LookChoice& l : r.looks) {
        std::vector<Member> m{{"style", Value::String(l.style)}};
        if (l.border) m.push_back({"border", RgbValue(*l.border, std::nullopt)});
        if (l.background) m.push_back({"background", RgbValue(*l.background, std::nullopt)});
        if (l.beam) m.push_back({"beam", RgbValue(*l.beam, std::nullopt)});
        looks.push_back(Value::Object(std::move(m)));
    }
    std::string out = "{\n";
    out += "  \"formatVersion\": 1,\n";
    out += "  \"about\": \"Your answers to the ESR Filter Editor's new-filter questions. The editor makes this filter from them.\",\n";
    out += "  \"name\": " + document::EncodeString(r.name) + ",\n";
    out += "  \"author\": " + document::EncodeString(r.author) + ",\n";
    out += "  \"minimal\": " + std::string(r.minimal ? "true" : "false") + ",\n";
    out += "  \"levels\": " + document::Render(Value::Array(std::move(levels))) + ",\n";
    out += "  \"defaultLevel\": " + std::to_string(r.defaultLevel) + ",\n";
    out += "  \"soundVolume\": " + std::to_string(r.soundVolume) + ",\n";
    out += "  \"parts\": [\n";
    for (std::size_t i = 0; i < parts.size(); ++i) out += "    " + document::Render(parts[i]) + (i + 1 < parts.size() ? ",\n" : "\n");
    out += "  ],\n";
    out += "  \"looks\": " + document::Render(Value::Array(std::move(looks))) + "\n";
    out += "}\n";
    return out;
}

// ---- making the filter ----

namespace {

auto Fail(std::string& error, const std::string& what, const document::Status& status) -> bool {
    error = what + ": " + status.error;
    return false;
}

// A row's levels on a rule: activeInLevels from..to, left out when that is every level.
auto SetLevels(document::Document& doc, int index, int from, int to, int levels, std::string& error) -> bool {
    from = std::max(1, from);
    to = to <= 0 || to > levels ? levels : to;
    if (from == 1 && to == levels) {
        if (doc.Rules()[static_cast<std::size_t>(index)].value.Find("activeInLevels") == nullptr) return true;
        const document::Status s = doc.RemoveField(index, "activeInLevels");
        return s ? true : Fail(error, "levels", s);
    }
    std::vector<Value> ids;
    for (int l = from; l <= to; ++l) ids.push_back(Value::Integer(l));
    const document::Status s = doc.SetField(index, "activeInLevels", Value::Array(std::move(ids)));
    return s ? true : Fail(error, "levels", s);
}

auto ApplyRow(document::Document& doc, int index, const Row& row, const StarterPart& part, const Starter& starter, int levels,
              std::string& error) -> bool {
    if (!SetLevels(doc, index, row.from, row.to, levels, error)) return false;
    Value rule = doc.Rules()[static_cast<std::size_t>(index)].value;
    // Character level.
    {
        std::vector<Value> stats;
        if (const Value* old = rule.Find("playerStats"); old != nullptr && old->IsArray())
            for (const Value& s : old->array)
                if (!(Str(s.Find("stat")) == "level" && Str(s.Find("op")) == ">=")) stats.push_back(s);
        if (row.clvl > 0)
            stats.push_back(Value::Object({{"stat", Value::String("level")}, {"op", Value::String(">=")}, {"value", Value::Integer(row.clvl)}}));
        const bool had = rule.Find("playerStats") != nullptr;
        if (!stats.empty()) {
            if (const document::Status s = doc.SetField(index, "playerStats", Value::Array(std::move(stats))); !s) return Fail(error, "character level", s);
        } else if (had) {
            if (const document::Status s = doc.RemoveField(index, "playerStats"); !s) return Fail(error, "character level", s);
        }
    }
    // Gold.
    if (row.amount >= 0) {
        std::vector<Value> stats;
        if (const Value* old = rule.Find("itemStats"); old != nullptr && old->IsArray())
            for (const Value& s : old->array)
                if (!(Str(s.Find("stat")) == "gold" && Str(s.Find("op")) == "<=")) stats.push_back(s);
        stats.push_back(Value::Object({{"stat", Value::String("gold")}, {"op", Value::String("<=")}, {"value", Value::Integer(row.amount)}}));
        if (const document::Status s = doc.SetField(index, "itemStats", Value::Array(std::move(stats))); !s) return Fail(error, "gold", s);
    }
    // Towns: a hide step always leaves them alone (in town every item is shown).
    if (part.kind == StarterPart::Kind::Hide) {
        const bool has = rule.Find("excludeAreas") != nullptr;
        if (!has && !starter.towns.empty()) {
            std::vector<Value> towns;
            for (std::int64_t t : starter.towns) towns.push_back(Value::Integer(t));
            if (const document::Status s = doc.SetField(index, "excludeAreas", Value::Array(std::move(towns)), "hide"); !s) return Fail(error, "towns", s);
        }
    }
    return true;
}

void DropIcons(Value& v) {
    if (v.IsObject()) {
        v.Erase("icon");
        for (Member& m : v.object) DropIcons(m.value);
    }
}

}  // namespace

auto MakeFilter(const Starter& starter, const Recipe& recipe, bool icons, std::string& text, std::string& error) -> bool {
    document::Document doc;
    std::vector<document::Diag> diags;
    if (!document::Document::Parse(starter.filterText, doc, diags)) {
        error = "the starter filter could not be read";
        return false;
    }
    const int levels = std::clamp(static_cast<int>(recipe.levels.size()), 1, 5);

    // The levels: the starter's translations kept for a name the player did not change.
    {
        std::vector<Value> list;
        const auto& old = starter.filter.GetSettings().filterLevels;
        for (int i = 0; i < levels; ++i) {
            const std::string name = recipe.levels[static_cast<std::size_t>(i)];
            Value nameValue = Value::String(name);
            if (static_cast<std::size_t>(i) < old.size()) {
                const Value& was = old[static_cast<std::size_t>(i)].name;
                const Value* en = was.IsObject() ? was.Find("enUS") : &was;
                if (Str(en) == name) nameValue = was;
            }
            list.push_back(Value::Object({{"id", Value::Integer(i + 1)}, {"name", nameValue}}));
        }
        if (const document::Status s = doc.SetSetting("filterLevels", Value::Array(std::move(list))); !s) return Fail(error, "levels", s);
        if (const document::Status s = doc.SetSetting("defaultLevel", Value::Integer(std::clamp(recipe.defaultLevel, 1, levels))); !s)
            return Fail(error, "default level", s);
    }
    const auto setting = [&](const char* key, Value v) {
        const document::Status s = doc.SetSetting(key, std::move(v));
        if (!s) error = std::string(key) + ": " + s.error;
        return static_cast<bool>(s);
    };
    if (!setting("title", Value::String(recipe.name)) || !setting("author", Value::String(recipe.author.empty() ? std::string("me") : recipe.author)) ||
        !setting("version", Value::String("1")) ||
        !setting("description", Value::String("Made with the ESR Filter Editor's new-filter questions, starting from " + starter.from + ".")) ||
        !setting("soundVolume", Value::Integer(std::clamp(recipe.soundVolume, 0, 100))))
        return false;
    if (doc.GetSettings().updateUrl.size() > 0)
        if (const document::Status s = doc.RemoveSetting("updateUrl"); !s) return Fail(error, "updateUrl", s);

    // The parts.
    for (const StarterPart& part : starter.parts) {
        const PartChoice* choice = recipe.Part(part.key);
        const bool on = choice == nullptr || choice->on;
        if (!on) {
            for (const std::string& id : part.rules) {
                const int index = doc.FindRule(id);
                if (index >= 0)
                    if (const document::Status s = doc.DeleteRule(index); !s) return Fail(error, "rule " + id, s);
            }
            continue;
        }
        if (choice == nullptr || choice->rows.empty()) continue;
        std::string last = part.rules.empty() ? std::string{} : part.rules.back();
        for (const Row& row : choice->rows) {
            int index = doc.FindRule(row.id);
            if (index < 0 && !row.copyOf.empty()) {  // a step the player added: a copy of a rule of the part
                const int source = starter.filter.FindRule(row.copyOf);
                const int after = doc.FindRule(last);
                if (source < 0 || after < 0) continue;
                Value copy = starter.filter.Rules()[static_cast<std::size_t>(source)].value;
                const std::string title = starter.filter.Rules()[static_cast<std::size_t>(source)].title;
                const std::size_t bar = title.find('|');
                copy.Set("comment", Value::String(row.id + " | " + (bar == std::string::npos ? title : title.substr(bar + 2)) + " (a step you added)"));
                if (const document::Status s = doc.InsertRule(document::Position::After(after), copy); !s) return Fail(error, "rule " + row.id, s);
                index = doc.FindRule(row.id);
            }
            if (index < 0) continue;
            if (!row.on) {
                if (const document::Status s = doc.DeleteRule(index); !s) return Fail(error, "rule " + row.id, s);
                continue;
            }
            const int to = row.to <= 0 || row.to > levels ? levels : row.to;
            if (row.from > to) {  // only on levels the filter no longer has
                if (const document::Status s = doc.DeleteRule(index); !s) return Fail(error, "rule " + row.id, s);
                continue;
            }
            last = row.id;
            if (!ApplyRow(doc, index, row, part, starter, levels, error)) return false;
        }
    }

    // Levels the filter no longer has: out of every rule; a rule left with none goes.
    for (int i = doc.ActiveCount() - 1; i >= 0; --i) {
        const Value* active = doc.Rules()[static_cast<std::size_t>(i)].value.Find("activeInLevels");
        if (active == nullptr || !(active->IsArray() || active->IsNumber())) continue;
        const std::vector<Value> list = active->IsArray() ? active->array : std::vector<Value>{*active};
        std::vector<Value> keep;
        for (const Value& l : list)
            if (Int(&l, 0) >= 1 && Int(&l, 0) <= levels) keep.push_back(l);
        if (keep.size() == list.size() && active->IsArray()) continue;
        if (keep.size() == list.size() && keep.size() < static_cast<std::size_t>(levels)) continue;  // a number still in range
        const document::Status s = keep.empty() ? doc.DeleteRule(i)
                                   : keep.size() == static_cast<std::size_t>(levels) ? doc.RemoveField(i, "activeInLevels")
                                                                                    : doc.SetField(i, "activeInLevels", Value::Array(std::move(keep)));
        if (!s) return Fail(error, "levels of a rule", s);
    }

    // The styles: the player's colours, icons out when there are none.
    {
        std::vector<Member> styles;
        for (const document::Style& st : doc.Styles()) {
            Value v = st.value;
            if (const LookChoice* look = recipe.Look(st.name)) {
                if (look->border) {
                    Value border = v.Find("labelBorder") != nullptr ? *v.Find("labelBorder") : Value::Object({{"width", Value::Integer(1)}});
                    border.Set("color", RgbValue(*look->border, 255));
                    v.Set("labelBorder", border);
                }
                if (look->background) {
                    std::int64_t alpha = 255;
                    if (const Value* old = v.Find("labelBackground"); old != nullptr && old->IsArray() && old->array.size() > 3) alpha = Int(&old->array[3], 255);
                    v.Set("labelBackground", RgbValue(*look->background, alpha));
                }
                if (look->beam) {
                    Value light = v.Find("droplight") != nullptr ? *v.Find("droplight") : Value::Object();
                    light.Set("color", RgbValue(*look->beam, std::nullopt));
                    v.Set("droplight", light);
                }
            }
            if (!icons) DropIcons(v);
            styles.push_back({st.name, std::move(v)});
        }
        if (!styles.empty())
            if (const document::Status s = doc.SetSetting("styles", Value::Object(std::move(styles))); !s) return Fail(error, "styles", s);
    }
    if (!icons) {
        for (int i = 0; i < doc.ActiveCount(); ++i) {
            if (doc.Rules()[static_cast<std::size_t>(i)].value.Find("icon") == nullptr) continue;
            if (const document::Status s = doc.RemoveField(i, "icon"); !s) return Fail(error, "icon", s);
        }
    }

    // The file's own heading.
    text = doc.Text();
    const std::size_t brace = text.find("\n{");
    const std::size_t start = text.rfind("//", 0) == 0 && brace != std::string::npos ? brace + 1 : 0;
    text = "// " + recipe.name + ": made with the ESR Filter Editor's new-filter questions, from " + starter.from +
           ".\n// Change it in the editor; to change the answers, use \"Change the answers\" there.\n" + text.substr(start);
    return true;
}

auto CreateFilter(const Folders& folders, const Starter& starter, const Recipe& recipe, std::string& mergedId, std::string& error) -> bool {
    if (!CheckVersionName(folders, recipe.name, error)) return false;
    // Icons from Sully's filter when the player has it (13 MB: not shipped with the editor).
    fs::path icons;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(P(folders.user), ec))
        if (entry.is_directory(ec) && fs::exists(entry.path() / "Sullys-QoL-Filter.json", ec) && fs::is_directory(entry.path() / "icons", ec))
            icons = entry.path() / "icons";
    std::string text;
    if (!MakeFilter(starter, recipe, !icons.empty(), text, error)) return false;

    const std::string baseId = "made:" + recipe.name + ".json";
    const std::string basePath = PathOfId(baseId, folders);
    if (basePath.empty()) {
        error = "that name cannot be a file name";
        return false;
    }
    const fs::path folder = P(basePath).parent_path();
    fs::create_directories(folder / "sounds", ec);
    for (const fs::directory_entry& entry : fs::directory_iterator(P(starter.folder) / "sounds", ec))
        if (entry.is_regular_file(ec)) fs::copy_file(entry.path(), folder / "sounds" / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
    if (!icons.empty()) {
        fs::create_directories(folder / "icons", ec);
        for (const fs::directory_entry& entry : fs::directory_iterator(icons, ec))
            if (entry.is_regular_file(ec)) fs::copy_file(entry.path(), folder / "icons" / entry.path().filename(), fs::copy_options::overwrite_existing, ec);
    }
    if (!WriteText(basePath, text) || !WriteText(U(folder / "recipe.json"), RenderRecipe(recipe))) {
        error = "could not write " + U(folder);
        return false;
    }
    Session session;
    if (!session.Open(baseId, basePath, folders, error) || !session.NameVersion(recipe.name, error)) return false;
    bool switched = false;
    if (!session.Save(error, switched)) return false;
    mergedId = session.MergedId();
    return true;
}

}  // namespace esrfe::edit
