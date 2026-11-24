#include "rule_text.h"

#include <algorithm>
#include <array>
#include <set>

#include "text_fold.hpp"
#include "ui_text.hpp"

namespace esrfe::ui {
namespace {

using document::Value;
using overlay::UiFormat;
using overlay::UiText;

constexpr std::size_t kNamesShown = 3;  // names in the one-line summary before "and N more"

// The five act towns (Rogue Encampment, Lut Gholein, Kurast Docks, Pandemonium Fortress, Harrogath).
const std::set<std::int64_t> kTowns{1, 40, 75, 103, 109};

auto Integers(const Value* v) -> std::vector<std::int64_t> {
    std::vector<std::int64_t> out;
    if (v == nullptr) return out;
    std::int64_t n = 0;
    if (v->AsInteger(n)) out.push_back(n);
    if (v->IsArray())
        for (const Value& e : v->array)
            if (e.AsInteger(n)) out.push_back(n);
    return out;
}

auto Strings(const Value* v) -> std::vector<std::string> {
    std::vector<std::string> out;
    if (v == nullptr) return out;
    if (v->IsString()) out.push_back(v->Str());
    if (v->IsArray())
        for (const Value& e : v->array)
            if (e.IsString()) out.push_back(e.Str());
    return out;
}

auto Lower(std::string s) -> std::string {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

// A game name on several lines ("(Light Belt, Snakecord)\nAncient Coupon") on one: the game draws the last line on
// top, so the lines go in reverse ("Ancient Coupon (Light Belt, Snakecord)").
auto OneLine(const std::string& name) -> std::string {
    if (name.find('\n') == std::string::npos) return name;
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t end = name.find('\n', start);
        std::string line = name.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty()) lines.push_back(std::move(line));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    std::string out;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) out += (out.empty() ? "" : " ") + *it;
    return out;
}

auto Truthy(const Value* v) -> bool { return v != nullptr && v->IsBool() && v->boolean; }

struct Named {
    std::int64_t number;
    const char* name;
    const char* english;
};
constexpr std::array<Named, 9> kQualities{{{1, "inferior", "Low quality"},
                                           {2, "normal", "Normal"},
                                           {3, "superior", "Superior"},
                                           {4, "magic", "Magic"},
                                           {5, "set", "Set"},
                                           {6, "rare", "Rare"},
                                           {7, "unique", "Unique"},
                                           {8, "crafted", "Crafted"},
                                           {9, "tempered", "Tempered"}}};
constexpr std::array<Named, 3> kTiers{{{0, "normal", "Normal tier"}, {1, "exceptional", "Exceptional"}, {2, "elite", "Elite"}}};
constexpr std::array<Named, 3> kDifficulties{{{0, "normal", "Normal difficulty"}, {1, "nightmare", "Nightmare"}, {2, "hell", "Hell"}}};

template <std::size_t N>
auto NamedWord(const Value& v, const std::array<Named, N>& table) -> std::string {
    std::int64_t n = -1;
    if (v.IsString()) {
        const std::string lowered = Lower(v.Str());
        for (const Named& e : table)
            if (lowered == e.name) return UiText(e.english);
        return v.Str();
    }
    if (v.AsInteger(n))
        for (const Named& e : table)
            if (n == e.number) return UiText(e.english);
    return document::Render(v);
}

template <std::size_t N>
auto NamedNumber(const Value& v, const std::array<Named, N>& table) -> std::int64_t {
    std::int64_t n = -1;
    if (v.AsInteger(n)) return n;
    if (v.IsString()) {
        const std::string lowered = Lower(v.Str());
        for (const Named& e : table)
            if (lowered == e.name) return e.number;
    }
    return -1;
}

// quality / baseTier / difficulty / itemLevel / sockets: one value, a list, or {"min", "max"}.
template <std::size_t N>
auto SpecWords(const Value& v, const std::array<Named, N>* table, const char* unitEnglish) -> std::string {
    const auto word = [&](const Value& e) { return table != nullptr ? NamedWord(e, *table) : document::Render(e); };
    if (v.IsArray()) {
        std::vector<std::string> words;
        for (const Value& e : v.array) words.push_back(word(e));
        const std::string list = JoinNames(words, words.size());
        return unitEnglish != nullptr ? UiFormat(unitEnglish, {list}) : list;
    }
    if (v.IsObject()) {
        const Value* lo = v.Find("min");
        const Value* hi = v.Find("max");
        std::string range;
        if (lo != nullptr && hi != nullptr) range = UiFormat("{0} to {1}", {word(*lo), word(*hi)});
        else if (lo != nullptr) range = UiFormat("{0} or more", {word(*lo)});
        else if (hi != nullptr) range = UiFormat("{0} or less", {word(*hi)});
        return unitEnglish != nullptr ? UiFormat(unitEnglish, {range}) : range;
    }
    return unitEnglish != nullptr ? UiFormat(unitEnglish, {word(v)}) : word(v);
}

class Describer {
public:
    Describer(const Value& rule, const DescribeContext& c) : rule_(rule), c_(c) {}

    auto Run() -> RuleSummary {
        Items();
        Conditions();
        Effects();
        Classify();
        Search();
        return std::move(s_);
    }

private:
    const Value& rule_;
    const DescribeContext& c_;
    RuleSummary s_;
    std::vector<std::string> codes_;
    bool wantsSet_ = false;

    auto Game() const -> const gamedata::GameData* { return c_.game; }

    // An action key on the rule, or on its style.
    auto Has(std::string_view key) const -> const Value* {
        if (const Value* v = rule_.Find(key)) return v;
        if (const Value* name = rule_.Find("useStyle"); name != nullptr && name->IsString() && c_.style) {
            if (const Value* style = c_.style(name->Str()); style != nullptr && style->IsObject()) return style->Find(key);
        }
        return nullptr;
    }

    auto BaseName(const std::string& code) const -> std::string {
        if (Game() == nullptr) return code;
        return OneLine(Game()->BaseName(code, c_.language));
    }

    auto TypeName(const std::string& code) const -> std::string {
        if (Game() == nullptr) return code;
        const int row = Game()->types.Row(code);
        if (row < 0) return code;
        const std::string name = Game()->types.DisplayName(row, Game()->strings, c_.language);
        return name.empty() ? code : name;
    }

    auto UniqueOrSetName(std::int64_t id) const -> std::string {
        if (Game() != nullptr) {
            const int row = static_cast<int>(id);
            if (!wantsSet_) {
                if (const gamedata::Unique* u = Game()->uniques.At(row)) return Game()->uniques.Name(*u, Game()->strings, c_.language);
            }
            if (const gamedata::SetItem* item = Game()->sets.Item(row)) return Game()->sets.Name(*item, Game()->strings, c_.language);
        }
        return "#" + std::to_string(id);
    }

    void Items() {
        if (const Value* q = rule_.Find("quality")) {
            if (NamedNumber(*q, kQualities) == 5) wantsSet_ = true;
        }
        codes_ = Strings(rule_.Find("itemCodes"));
        std::vector<std::string> names;
        for (std::int64_t id : Integers(rule_.Find("uniqueOrSetIds"))) names.push_back(UniqueOrSetName(id));
        for (const std::string& code : codes_) names.push_back(BaseName(code));
        for (const std::string& type : Strings(rule_.Find("itemTypes"))) names.push_back(TypeName(type));
        s_.whatAll = names;
        s_.what = names.empty() ? UiText("Any item") : JoinNames(names, kNamesShown);
    }

    void Add(std::string text) {
        if (!text.empty()) s_.conditions.push_back(std::move(text));
    }

    auto StatName(const Value& stat) const -> std::pair<int, std::string> {
        std::int64_t id = -1;
        if (stat.AsInteger(id)) {
        } else if (stat.IsString() && Game() != nullptr) {
            id = Game()->stats.Id(stat.Str());
        }
        std::string name = stat.IsString() ? stat.Str() : document::Render(stat);
        return {static_cast<int>(id), name};
    }

    // One itemStats / playerStats check in words.
    auto StatCheck(const Value& check, bool player) const -> std::string {
        if (!check.IsObject()) return {};
        const Value* stat = check.Find("stat");
        if (stat == nullptr) return {};
        const auto [id, rawName] = StatName(*stat);
        std::int64_t param = 0;
        if (const Value* p = check.Find("param")) p->AsInteger(param);
        const Value* opValue = check.Find("op");
        const std::string op = opValue != nullptr && opValue->IsString() ? opValue->Str() : ">=";
        const Value* value = check.Find("value");
        std::int64_t number = 0, high = 0;
        bool between = false;
        if (value != nullptr && value->IsArray() && value->array.size() == 2) {
            value->array[0].AsInteger(number);
            value->array[1].AsInteger(high);
            between = true;
        } else if (value != nullptr) {
            value->AsInteger(number);
        }

        if (player && Lower(rawName) == "level") {
            const std::string n = std::to_string(number);
            if (between) return UiFormat("Character level {0} to {1}", {n, std::to_string(high)});
            if (op == ">=") return UiFormat("Character level {0} or higher", {n});
            if (op == ">") return UiFormat("Character level above {0}", {n});
            if (op == "<=") return UiFormat("Character level {0} or lower", {n});
            if (op == "<") return UiFormat("Character level below {0}", {n});
            if (op == "!=") return UiFormat("Character level not {0}", {n});
            return UiFormat("Character level {0}", {n});
        }

        if (Lower(rawName) == "gold" && !between) {
            const std::string n = std::to_string(number);
            if (op == ">=") return UiFormat("{0} gold or more", {n});
            if (op == "<=") return UiFormat("{0} gold or less", {n});
            if (op == "<") return UiFormat("Less than {0} gold", {n});
            if (op == ">") return UiFormat("More than {0} gold", {n});
        }

        // The tooltip line for the value ("+1 to Maximum Curses"), else the stat's name.
        std::string line;
        if (Game() != nullptr && id >= 0) {
            line = gamedata::Strings::Plain(Game()->StatLine(id, static_cast<int>(number), static_cast<int>(param), c_.language));
        }
        const bool named = !line.empty();
        if (!named) line = rawName + " " + std::to_string(number);
        std::string text;
        if (between) {
            std::string highLine;
            if (named) highLine = gamedata::Strings::Plain(Game()->StatLine(id, static_cast<int>(high), static_cast<int>(param), c_.language));
            text = named ? UiFormat("{0} up to {1}", {line, highLine})
                         : UiFormat("{0} {1} to {2}", {rawName, std::to_string(number), std::to_string(high)});
        } else if (op == ">=") {
            text = UiFormat("{0} or more", {line});
        } else if (op == ">") {
            text = UiFormat("More than {0}", {line});
        } else if (op == "<=") {
            text = UiFormat("{0} or less", {line});
        } else if (op == "<") {
            text = UiFormat("Less than {0}", {line});
        } else if (op == "!=") {
            text = UiFormat("Not {0}", {line});
        } else {
            text = UiFormat("Exactly {0}", {line});
        }
        return player ? UiFormat("Your character: {0}", {text}) : text;
    }

    void StatList(const char* key, bool player, bool anyOf) {
        const Value* list = rule_.Find(key);
        if (list == nullptr || !list->IsArray()) return;
        std::vector<std::string> checks;
        for (const Value& check : list->array) {
            std::string text = StatCheck(check, player);
            if (!text.empty()) checks.push_back(std::move(text));
        }
        if (checks.empty()) return;
        if (!anyOf) {
            for (std::string& text : checks) Add(std::move(text));
            return;
        }
        std::string joined;
        for (const std::string& text : checks) joined += (joined.empty() ? "" : UiText(" or ")) + text;
        Add(UiFormat("Any of: {0}", {joined}));
    }

    auto AreaList(const std::vector<std::int64_t>& areas) const -> std::string {
        std::vector<std::string> names;
        for (std::int64_t id : areas) {
            std::string name = Game() != nullptr ? Game()->AreaName(static_cast<int>(id), c_.language) : std::string{};
            names.push_back(name.empty() ? UiFormat("area {0}", {std::to_string(id)}) : name);
        }
        return JoinNames(names, kNamesShown);
    }

    void Conditions() {
        if (const Value* v = rule_.Find("quality")) Add(SpecWords(*v, &kQualities, nullptr));
        if (const Value* v = rule_.Find("baseTier")) Add(SpecWords(*v, &kTiers, nullptr));
        if (const Value* v = rule_.Find("isEthereal"); v != nullptr && v->IsBool())
            Add(v->boolean ? UiText("Ethereal") : UiText("Not ethereal"));
        if (const Value* v = rule_.Find("isIdentified"); v != nullptr && v->IsBool())
            Add(v->boolean ? UiText("Identified") : UiText("Unidentified"));
        if (const Value* v = rule_.Find("isRuneword"); v != nullptr && v->IsBool())
            Add(v->boolean ? UiText("A runeword") : UiText("Not a runeword"));
        if (const Value* v = rule_.Find("isFullySocketed"); v != nullptr && v->IsBool())
            Add(v->boolean ? UiText("Every socket filled") : UiText("Sockets left to fill"));
        if (const Value* v = rule_.Find("sockets")) Add(SpecWords<1>(*v, nullptr, "Sockets: {0}"));
        if (const Value* v = rule_.Find("maxSockets")) Add(SpecWords<1>(*v, nullptr, "Can have sockets: {0}"));
        if (const Value* v = rule_.Find("itemLevel")) Add(SpecWords<1>(*v, nullptr, "Item level {0}"));
        StatList("itemStats", false, false);
        StatList("itemStatsAnyOf", false, true);
        StatList("playerStats", true, false);
        StatList("playerStatsAnyOf", true, true);
        if (const Value* v = rule_.Find("difficulty")) Add(SpecWords(*v, &kDifficulties, nullptr));
        if (const Value* v = rule_.Find("chronicle"); v != nullptr && v->IsString()) {
            const std::string c = Lower(v->Str());
            Add(c == "missing" ? UiText("Not in your Chronicle yet") : c == "completed" ? UiText("Already in your Chronicle") : v->Str());
        }
        {
            const std::vector<std::int64_t> areas = Integers(rule_.Find("areas"));
            const std::vector<std::int64_t> excluded = Integers(rule_.Find("excludeAreas"));
            const std::set<std::int64_t> inSet(areas.begin(), areas.end()), outSet(excluded.begin(), excluded.end());
            if (!areas.empty()) Add(inSet == kTowns ? UiText("In towns") : UiFormat("In: {0}", {AreaList(areas)}));
            if (!excluded.empty()) Add(outSet == kTowns ? UiText("Outside towns") : UiFormat("Not in: {0}", {AreaList(excluded)}));
        }
        {
            std::vector<std::string> places;
            for (const std::string& p : Strings(rule_.Find("location"))) {
                if (p == "onGround") places.push_back(UiText("on the ground"));
                else if (p == "stored") places.push_back(UiText("in your inventory or stash"));
                else if (p == "equipped") places.push_back(UiText("equipped"));
                else if (p == "atVendor") places.push_back(UiText("at a vendor"));
                else places.push_back(p);
            }
            if (!places.empty()) Add(UiFormat("Where: {0}", {JoinNames(places, places.size())}));
        }
        if (rule_.Find("formula") != nullptr) Add(UiText("A formula is true"));
        {
            std::vector<std::string> except;
            for (const std::string& code : Strings(rule_.Find("excludeItemCodes"))) except.push_back(BaseName(code));
            for (const std::string& type : Strings(rule_.Find("excludeItemTypes"))) except.push_back(TypeName(type));
            for (std::int64_t id : Integers(rule_.Find("excludeUniqueOrSetIds"))) except.push_back(UniqueOrSetName(id));
            if (!except.empty()) Add(UiFormat("Except: {0}", {JoinNames(except, kNamesShown)}));
        }
        s_.levels = Integers(rule_.Find("activeInLevels"));
        if (!s_.levels.empty()) {
            std::vector<std::string> names;
            for (std::int64_t id : s_.levels) {
                std::string name = c_.levelName ? c_.levelName(id) : std::string{};
                names.push_back(name.empty() ? UiFormat("level {0}", {std::to_string(id)}) : name);
            }
            Add(UiFormat("Only on: {0}", {JoinNames(names, names.size())}));
        }
    }

    // A name text with its placeholders filled where the rule tells us enough.
    auto NameText(const Value& text) const -> std::string {
        std::string out = PickLanguage(text, c_.language);
        std::size_t pos = 0;
        while ((pos = out.find('{', pos)) != std::string::npos) {
            const std::size_t end = out.find('}', pos + 1);
            if (end == std::string::npos) break;
            const std::string inner = out.substr(pos + 1, end - pos - 1);
            std::string with;
            if (inner.rfind("string:", 0) == 0 && Game() != nullptr) {
                const std::string key = inner.substr(7);
                const std::string* found = Game()->strings.Find(key, c_.language);
                with = found != nullptr ? OneLine(*found) : BaseName(key);
            } else if (inner.rfind("stat:", 0) == 0 || inner.rfind("playerStat:", 0) == 0) {
                with = "#";  // the item's own value
            } else if (inner == "name") {
                with = codes_.size() == 1 ? BaseName(codes_.front()) : UiText("(its name)");
            } else {
                pos = end + 1;
                continue;
            }
            out.replace(pos, end - pos + 1, with);
            pos += with.size();
        }
        return out;
    }

    void Effects() {
        const Value* replace = Has("replaceName");
        const Value* prefix = Has("namePrefix");
        const Value* suffix = Has("nameSuffix");
        if (replace != nullptr || prefix != nullptr || suffix != nullptr) {
            std::string name = replace != nullptr ? NameText(*replace)
                                                  : (codes_.size() == 1 ? BaseName(codes_.front()) : UiText("(its name)"));
            if (prefix != nullptr) name = NameText(*prefix) + name;
            if (suffix != nullptr) name += NameText(*suffix);
            s_.preview = name;
            s_.effects.push_back(UiFormat("Shown as: {0}", {name}));
        }
        if (Has("labelBackground") != nullptr || Has("labelBorder") != nullptr) s_.effects.push_back(UiText("Coloured label"));
        if (Has("droplight") != nullptr) s_.effects.push_back(UiText("Light beam where it drops"));
        if (Has("icon") != nullptr) s_.effects.push_back(UiText("Map icon"));
        if (Has("sound") != nullptr) s_.effects.push_back(UiText("Plays a sound"));
        if (Has("chatMessage") != nullptr) s_.effects.push_back(UiText("Chat message"));
        if (Has("chatMessageWhenHidden") != nullptr) s_.effects.push_back(UiText("Chat message when hidden"));
        if (Has("notify") != nullptr) s_.effects.push_back(UiText("Notification"));
        s_.hideWhenOthersFail = Truthy(Has("hideIfOtherConditionsFail"));
        if (s_.hideWhenOthersFail) s_.effects.push_back(UiText("Hidden when the other conditions are not met"));
    }

    void Classify() {
        const bool decorates = Has("labelBackground") != nullptr || Has("labelBorder") != nullptr || Has("droplight") != nullptr ||
                               Has("icon") != nullptr || Has("sound") != nullptr || Has("chatMessage") != nullptr ||
                               Has("notify") != nullptr;
        const bool renames = !s_.preview.empty();
        if (Truthy(Has("hide"))) s_.action = Action::Hide;
        else if (Truthy(Has("show"))) s_.action = Action::Show;
        else if (decorates) s_.action = Action::Highlight;
        else if (renames) s_.action = Action::Rename;
        else if (s_.hideWhenOthersFail) s_.action = Action::Hide;
        else s_.action = Action::Other;
        switch (s_.action) {
            case Action::Hide: s_.actionLabel = UiText("Hide"); break;
            case Action::Show: s_.actionLabel = UiText("Always show"); break;
            case Action::Highlight: s_.actionLabel = UiText("Highlight"); break;
            case Action::Rename: s_.actionLabel = UiText("Rename"); break;
            case Action::Other: s_.actionLabel = UiText("Other"); break;
        }
    }

    void Search() {
        std::string all = s_.actionLabel + "\n" + s_.what + "\n";
        for (const std::string& n : s_.whatAll) all += n + "\n";
        for (const std::string& n : s_.conditions) all += n + "\n";
        for (const std::string& n : s_.effects) all += n + "\n";
        for (const std::string& code : codes_) all += code + "\n";
        for (const std::string& type : Strings(rule_.Find("itemTypes"))) all += type + "\n";
        s_.search = all;
    }
};

}  // namespace

auto PickLanguage(const Value& text, gamedata::Language language) -> std::string {
    if (text.IsString()) return text.Str();
    if (!text.IsObject() || text.object.empty()) return {};
    if (const Value* v = text.Find(gamedata::LanguageCode(language)); v != nullptr && v->IsString()) return v->Str();
    if (const Value* v = text.Find("enUS"); v != nullptr && v->IsString()) return v->Str();
    for (const document::Member& m : text.object)
        if (m.value.IsString()) return m.value.Str();
    return {};
}

auto JoinNames(const std::vector<std::string>& names, std::size_t shown) -> std::string {
    if (names.empty()) return {};
    if (names.size() == 1) return names.front();
    if (names.size() > shown && shown > 0) {
        std::string head;
        for (std::size_t i = 0; i < shown; ++i) head += (i == 0 ? "" : ", ") + names[i];
        return UiFormat("{0} and {1} more", {head, std::to_string(names.size() - shown)});
    }
    std::string head;
    for (std::size_t i = 0; i + 1 < names.size(); ++i) head += (i == 0 ? "" : ", ") + names[i];
    return UiFormat("{0} and {1}", {head, names.back()});
}

auto Describe(const Value& rule, const DescribeContext& context) -> RuleSummary {
    return Describer(rule, context).Run();
}

}  // namespace esrfe::ui
