#include "rule_draft.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <unordered_map>

#include "groups.h"

namespace esrfe::ui {

using document::Value;
using gamedata::Tier;

namespace {

const char* const kTierNames[] = {"", "normal", "exceptional", "elite"};  // by Tier
constexpr const char* kQualityNames[] = {"", "inferior", "normal", "superior", "magic", "set", "rare", "unique", "crafted", "tempered"};

auto TierOf(const gamedata::Base& b) -> Tier { return b.mythical ? Tier::Elite : b.tier; }

auto Pickable(const gamedata::GameData& game, int stat, int param) -> bool {
    const gamedata::Stat* s = game.stats.At(stat);
    if (s == nullptr || param < 0) return false;
    return s->param != gamedata::ParamKind::SkillLevel;  // chance to cast, charges: the skill's level is in the param
}

auto Plain(const std::string& text) -> std::string { return gamedata::Strings::Plain(text); }

auto Rgba(const Rgb& c, int a = 255) -> Value {
    return Value::Array({Value::Integer(c.r), Value::Integer(c.g), Value::Integer(c.b), Value::Integer(a)});
}

auto ReadRgb(const Value* v, Rgb& out) -> bool {
    if (v == nullptr || !v->IsArray() || v->array.size() < 3) return false;
    std::int64_t r = 0, g = 0, b = 0;
    v->array[0].AsInteger(r);
    v->array[1].AsInteger(g);
    v->array[2].AsInteger(b);
    out = Rgb{static_cast<int>(r), static_cast<int>(g), static_cast<int>(b)};
    return true;
}

// The group a base is listed in ("weapons", "armor", "runes", ...).
auto GroupId(const gamedata::GameData& game, const std::string& code) -> std::string {
    const gamedata::GroupRef ref = game.groups.Of(code);
    if (ref.group < 0 || ref.group >= static_cast<int>(game.groups.All().size())) return {};
    return game.groups.All()[static_cast<std::size_t>(ref.group)].id;
}

}  // namespace

auto Swatches() -> const std::vector<NamedColor>& {
    static const std::vector<NamedColor> colors{
        {"Gold", {255, 200, 0}},    {"Red", {230, 50, 50}},    {"Orange", {255, 140, 0}},  {"Yellow", {255, 240, 80}},
        {"Green", {60, 210, 90}},   {"Blue", {70, 130, 255}},  {"Purple", {180, 80, 255}}, {"Pink", {255, 110, 200}},
        {"White", {255, 255, 255}}, {"Grey", {130, 130, 130}}, {"Black", {0, 0, 0}},
    };
    return colors;
}

auto NameColors() -> const std::vector<NameColor>& {
    static const std::vector<NameColor> colors{
        {"White", '0', {255, 255, 255}},      {"Red", '1', {255, 77, 77}},            {"Set", '2', {0, 255, 0}},
        {"Magic", '3', {105, 105, 255}}, {"Unique", '4', {199, 179, 119}}, {"Grey", '5', {105, 105, 105}},
        {"Orange", '8', {255, 168, 0}},       {"Rare", '9', {255, 255, 100}}, {"Dark green", ':', {0, 128, 0}},
        {"Purple", ';', {174, 0, 255}},
    };
    return colors;
}

auto KindChoices() -> const std::vector<std::pair<ItemKind, const char*>>& {
    static const std::vector<std::pair<ItemKind, const char*>> kinds{
        {ItemKind::Base, "White and superior bases"}, {ItemKind::Magic, "Magic items"}, {ItemKind::Rare, "Rare items"},
        {ItemKind::Unique, "Unique items"},           {ItemKind::Set, "Set items"},    {ItemKind::Other, "Runes, gems and other items"},
    };
    return kinds;
}

auto Coupons(const gamedata::GameData& game) -> const std::vector<Coupon>& {
    // Worked out once per game data (the editor has one).
    static std::mutex lock;
    static const gamedata::GameData* builtFor = nullptr;
    static std::vector<Coupon> coupons;
    std::lock_guard<std::mutex> guard(lock);
    if (builtFor == &game) return coupons;
    builtFor = &game;
    coupons.clear();
    std::unordered_map<std::string, std::string> baseByName;  // English base name -> code, weapons and armor
    for (const gamedata::Base& b : game.bases.All()) {
        if (b.tier == Tier::None) continue;
        const std::string* name = game.strings.Find(b.code, gamedata::Language::enUS);
        if (name != nullptr) baseByName.emplace(Plain(*name), b.code);
    }
    for (const gamedata::Base& b : game.bases.All()) {
        const std::string* name = game.strings.Find(b.code, gamedata::Language::enUS);
        if (name == nullptr) continue;
        const std::string plain = Plain(*name);
        if (plain.find("Ancient Coupon") == std::string::npos) continue;
        Coupon c;
        c.code = b.code;
        const std::size_t open = plain.find('(');
        const std::size_t comma = plain.find(',', open == std::string::npos ? 0 : open);
        if (open != std::string::npos && comma != std::string::npos) {
            const auto it = baseByName.find(plain.substr(open + 1, comma - open - 1));
            if (it != baseByName.end()) {
                c.base = it->second;
                if (const gamedata::Base* base = game.bases.Find(c.base)) c.tier = TierOf(*base);
            }
        }
        coupons.push_back(std::move(c));
    }
    return coupons;
}

auto IsCoupon(const gamedata::GameData& game, const std::string& code) -> bool {
    for (const Coupon& c : Coupons(game))
        if (c.code == code) return true;
    return false;
}

auto KindAllows(ItemKind kind, const gamedata::GameData& game, const gamedata::Base& b) -> bool {
    const std::string group = GroupId(game, b.code);
    switch (kind) {
        case ItemKind::Base: return (group == "weapons" || group == "armor") && (b.rarities.normal || b.rarities.superior);
        case ItemKind::Magic: return b.rarities.magic;
        case ItemKind::Rare: return b.rarities.rare;
        case ItemKind::Other:
            return !b.rarities.magic && !b.rarities.rare && group != "weapons" && group != "armor" && !IsCoupon(game, b.code);
        default: return false;
    }
}

auto RuneColor(const gamedata::GameData& game, const std::string& code) -> char {
    const gamedata::Base* b = game.bases.Find(code);
    if (b == nullptr || b->type != "rune" || (!code.empty() && code[0] == 'x')) return 0;  // x..: crystals
    const std::string& name = game.strings.Text(b->nameKey, gamedata::Language::enUS);  // as the game shows it
    const std::size_t at = name.find("\xC3\xBF" "c");
    return at != std::string::npos && at + 3 < name.size() ? name[at + 3] : 0;
}

auto RuneColorName(char color) -> std::string {
    switch (color) {
        case '0': return "White";
        case '1': return "Red";
        case '2': return "Green";
        case '3': return "Blue";
        case '4': return "Gold";
        case '5': return "Grey";
        case '8': return "Orange";
        case '9': return "Yellow";
        case ':': return "Dark green";
        case ';': return "Purple";
        case 'N': return "Elemental";  // the turquoise runes (Moon to God): ESR calls them Elemental Runes
        default: return "Other";
    }
}

auto TierOfUnique(const gamedata::GameData& game, const std::string& baseCode) -> Tier {
    const gamedata::Base* b = game.bases.Find(baseCode);
    return b == nullptr ? Tier::None : TierOf(*b);
}

auto ChosenBases(const RuleDraft& draft, const gamedata::GameData& game) -> std::vector<std::string> {
    std::vector<std::string> out;
    for (const std::string& code : draft.bases) {
        const gamedata::Base* b = game.bases.Find(code);
        if (b == nullptr) continue;
        if (draft.mythical == RuleDraft::Mythicals::Leave && b->mythical) continue;
        if (draft.mythical == RuleDraft::Mythicals::Only && !b->mythical) continue;
        if (!draft.tiers.empty() && TierOf(*b) != Tier::None && draft.tiers.count(TierOf(*b)) == 0) continue;
        out.push_back(code);
    }
    return out;
}

auto Offer(const RuleDraft& draft, const gamedata::GameData& game) -> DraftOffer {
    DraftOffer offer;
    const bool byBase = draft.kind == ItemKind::Base || draft.kind == ItemKind::Magic || draft.kind == ItemKind::Rare;
    if (byBase) {
        for (const std::string& code : draft.bases) {
            const gamedata::Base* b = game.bases.Find(code);
            if (b == nullptr) continue;
            if (b->mythical) offer.mythicals = true;
            if (TierOf(*b) != Tier::None) offer.tierChoices.insert(TierOf(*b));
        }
        offer.tiers = offer.tierChoices.size() > 1 || offer.mythicals;
    }
    offer.qualities = draft.kind == ItemKind::Base;
    const std::vector<std::string> chosen = ChosenBases(draft, game);
    const bool superior = draft.kind == ItemKind::Base && draft.baseQualities == std::set<int>{3};
    const bool magic = draft.kind == ItemKind::Magic;
    const bool rare = draft.kind == ItemKind::Rare;
    offer.statsPossible = (superior || magic || rare) && !chosen.empty();
    if (!offer.statsPossible) {
        if (draft.kind == ItemKind::Base) offer.statsWhy = "Stats are for superior bases: choose Superior only to filter by them.";
        return offer;
    }
    const std::vector<gamedata::StatRange> ranges = superior ? game.SuperiorStatsFor(chosen) : rare ? game.RareStatsFor(chosen) : game.MagicStatsFor(chosen);
    for (const gamedata::StatRange& r : ranges) {
        if (!Pickable(game, r.stat, r.param) || r.max <= 0) continue;
        const gamedata::Stat* info = game.stats.At(r.stat);
        // Durations belong to their damage line (the game never shows them alone).
        if (info != nullptr && (info->name == "coldlength" || info->name == "poisonlength")) continue;
        // "item_aura_display" and "item_nonclassskill_display" only show the line: the same property gives the real stat too.
        if (info != nullptr && info->name.size() > 8 && info->name.compare(info->name.size() - 8, 8, "_display") == 0) continue;
        StatOption o;
        o.stat = r.stat;
        o.param = r.param;
        o.min = std::max(1, r.min);  // a bonus that can also be a malus (bolts and crossbow damage): its positive part
        o.max = r.max;
        o.label = Plain(game.StatLine(r.stat, r.max, r.param, game.language));
        if (o.label.empty() && info != nullptr && info->name == "item_numsockets") o.label = "Sockets (" + std::to_string(r.max) + ")";
        if (o.label.empty()) o.label = (info != nullptr ? info->name : std::to_string(r.stat)) + " " + std::to_string(r.max);
        offer.stats.push_back(std::move(o));
    }
    std::sort(offer.stats.begin(), offer.stats.end(), [](const StatOption& a, const StatOption& b) { return a.label < b.label; });
    return offer;
}

void Tidy(RuleDraft& draft, const gamedata::GameData& game) {
    // What the kind does not use goes.
    const bool byBase = draft.kind == ItemKind::Base || draft.kind == ItemKind::Magic || draft.kind == ItemKind::Rare || draft.kind == ItemKind::Other;
    if (!byBase) {
        draft.bases.clear();
    } else {
        draft.bases.erase(std::remove_if(draft.bases.begin(), draft.bases.end(),
                                         [&](const std::string& c) {
                                             const gamedata::Base* b = game.bases.Find(c);
                                             return b == nullptr || !KindAllows(draft.kind, game, *b);
                                         }),
                          draft.bases.end());
    }
    if (!byBase) draft.types.clear();
    // Every item: only where the quality alone says what it is (white items include runes and gems).
    if (draft.kind == ItemKind::Base || draft.kind == ItemKind::Other || draft.kind == ItemKind::None) draft.every = false;
    if (draft.every) {
        draft.bases.clear();
        draft.types.clear();
        draft.uniques.clear();
        draft.coupons.clear();
        draft.setItems.clear();
    }
    if (draft.kind != ItemKind::Unique) {
        draft.uniques.clear();
        draft.coupons.clear();
    }
    if (draft.kind != ItemKind::Set) draft.setItems.clear();
    if (draft.kind != ItemKind::Base) draft.baseQualities.clear();
    if (draft.baseQualities.size() == 2) draft.baseQualities.clear();
    if (draft.action == DraftAction::AlwaysShow) draft.levels.clear();
    if (draft.action == DraftAction::Hide) draft.look = Look{};

    const DraftOffer offer = Offer(draft, game);
    if (draft.types.empty())  // by item type, the tier is not limited to the bases chosen
        for (auto it = draft.tiers.begin(); it != draft.tiers.end();)
            it = offer.tierChoices.count(*it) ? std::next(it) : draft.tiers.erase(it);
    if (!offer.mythicals) draft.mythical = RuleDraft::Mythicals::Include;
    const DraftOffer after = Offer(draft, game);
    std::vector<StatPick> kept;
    for (const StatPick& p : draft.stats) {
        for (const StatOption& o : after.stats) {
            if (o.stat != p.stat || o.param != p.param) continue;
            StatPick q = p;
            q.atLeast = std::clamp(q.atLeast, o.min, o.max);
            kept.push_back(q);
        }
    }
    draft.stats = after.statsPossible ? kept : std::vector<StatPick>{};
}

auto PickCount(const RuleDraft& draft) -> std::size_t {
    return draft.bases.size() + draft.types.size() + (draft.every ? 1 : 0) + draft.uniques.size() + draft.coupons.size() + draft.setItems.size();
}

auto Complete(const RuleDraft& draft) -> bool {
    return draft.action != DraftAction::None && draft.kind != ItemKind::None && PickCount(draft) > 0;
}

namespace {

void PutLook(Value& rule, const Look& look) {
    if (look.border) rule.Set("labelBorder", Value::Object({{"color", Rgba(*look.border)}, {"width", Value::Integer(2)}}));
    if (look.background) rule.Set("labelBackground", Rgba(*look.background, 230));
    if (look.nameColor >= 0 && look.nameColor < static_cast<int>(NameColors().size()))
        rule.Set("namePrefix", Value::String(std::string("\xC3\xBF" "c") + NameColors()[static_cast<std::size_t>(look.nameColor)].code));
    if (look.beam)
        rule.Set("droplight", Value::Object({{"color", Value::Array({Value::Integer(look.beamColor.r), Value::Integer(look.beamColor.g),
                                                                      Value::Integer(look.beamColor.b)})}}));
    if (!look.sound.empty()) rule.Set("sound", Value::String(look.sound));
    if (!look.icon.empty())
        rule.Set("icon", Value::Object({{"iconName", Value::String(look.icon)}, {"iconSize", Value::Integer(std::clamp(look.iconSize, 4, 256))}}));
    if (look.chat) {
        const int c = std::clamp(look.chatColor, 0, static_cast<int>(NameColors().size()) - 1);
        rule.Set("chatMessage", Value::String(std::string("\xC3\xBF" "c") + NameColors()[static_cast<std::size_t>(c)].code + look.chatText));
    }
}

void PutCommon(Value& rule, const RuleDraft& draft, const gamedata::GameData& game) {
    if (!draft.stats.empty()) {
        Value checks = Value::Array();
        for (const StatPick& p : draft.stats) {
            const gamedata::Stat* s = game.stats.At(p.stat);
            Value check = Value::Object();
            check.Set("stat", s != nullptr && !s->name.empty() ? Value::String(s->name) : Value::Integer(p.stat));
            if (p.param != 0) check.Set("param", Value::Integer(p.param));
            check.Set("op", Value::String(">="));
            check.Set("value", Value::Integer(p.atLeast));
            checks.array.push_back(std::move(check));
        }
        rule.Set("itemStats", std::move(checks));
        rule.Set("isIdentified", Value::Bool(true));  // the stats of an unidentified item are not known
    }
    if (!draft.levels.empty() && draft.action != DraftAction::AlwaysShow) {
        Value levels = Value::Array();
        for (std::int64_t id : draft.levels) levels.array.push_back(Value::Integer(id));
        rule.Set("activeInLevels", std::move(levels));
    }
}

auto Finish(Value rule, const RuleDraft& draft) -> edit::OwnRule {
    edit::OwnRule own;
    if (draft.action == DraftAction::Hide) {
        Value towns = Value::Array();
        for (std::int64_t t : TownAreas()) towns.array.push_back(Value::Integer(t));
        rule.Set("excludeAreas", std::move(towns));
        rule.Set("hide", Value::Bool(true));
        own.place = "top";
    } else if (draft.look.Any()) {  // the look last (its colours win), a show-only copy first (never hidden)
        PutLook(rule, draft.look);
        own.place = "end";
        own.keepShown = true;
    } else {
        rule.Set("show", Value::Bool(true));
        own.place = "top";
    }
    own.rule = std::move(rule);
    return own;
}

auto StringList(const std::vector<std::string>& list) -> Value {
    Value a = Value::Array();
    for (const std::string& s : list) a.array.push_back(Value::String(s));
    return a;
}

auto IntList(const std::vector<int>& list) -> Value {
    Value a = Value::Array();
    for (int i : list) a.array.push_back(Value::Integer(i));
    return a;
}

}  // namespace

auto ToRules(const RuleDraft& draft, const gamedata::GameData& game, const std::string& summary) -> std::vector<edit::OwnRule> {
    std::vector<edit::OwnRule> out;
    const char* word = draft.action == DraftAction::Hide ? "hide" : draft.action == DraftAction::AlwaysShow ? "always show" : "show";
    const auto start = [&](const std::string& extra) {
        Value rule = Value::Object();
        rule.Set("comment", Value::String(summary + extra + " | " + word));
        return rule;
    };
    switch (draft.kind) {
        case ItemKind::Base:
        case ItemKind::Magic:
        case ItemKind::Rare:
        case ItemKind::Other: {
            Value rule = start("");
            std::vector<std::string> codes;
            for (const std::string& code : draft.bases) {
                const gamedata::Base* b = game.bases.Find(code);
                if (draft.mythical == RuleDraft::Mythicals::Only && (b == nullptr || !b->mythical)) continue;
                codes.push_back(code);
            }
            if (!draft.types.empty()) rule.Set("itemTypes", StringList(draft.types));
            else if (!draft.every) rule.Set("itemCodes", StringList(codes));
            if (draft.mythical == RuleDraft::Mythicals::Leave) {
                std::vector<std::string> leave;
                for (const std::string& code : codes) {
                    const gamedata::Base* b = game.bases.Find(code);
                    if (b != nullptr && b->mythical) leave.push_back(code);
                }
                if (!leave.empty()) rule.Set("excludeItemCodes", StringList(leave));
            }
            const DraftOffer offer = Offer(draft, game);
            if (!draft.tiers.empty() && draft.tiers != offer.tierChoices) {
                Value tiers = Value::Array();
                for (Tier t : draft.tiers) tiers.array.push_back(Value::String(kTierNames[static_cast<int>(t)]));
                rule.Set("baseTier", tiers.array.size() == 1 ? tiers.array[0] : tiers);
            }
            if (draft.kind == ItemKind::Base) {
                const std::set<int> q = draft.baseQualities.empty() ? std::set<int>{2, 3} : draft.baseQualities;
                Value list = Value::Array();
                for (int quality : q) list.array.push_back(Value::String(kQualityNames[quality]));
                rule.Set("quality", list.array.size() == 1 ? list.array[0] : list);
            } else if (draft.kind == ItemKind::Magic) {
                rule.Set("quality", Value::String("magic"));
            } else if (draft.kind == ItemKind::Rare) {
                rule.Set("quality", Value::String("rare"));
            }
            PutCommon(rule, draft, game);
            out.push_back(Finish(std::move(rule), draft));
            break;
        }
        case ItemKind::Unique: {
            if (draft.every) {  // every unique: the quality alone
                Value rule = start("");
                rule.Set("quality", Value::String("unique"));
                PutCommon(rule, draft, game);
                out.push_back(Finish(std::move(rule), draft));
                break;
            }
            if (!draft.uniques.empty()) {
                Value rule = start("");
                rule.Set("quality", Value::String("unique"));
                rule.Set("uniqueOrSetIds", IntList(draft.uniques));
                PutCommon(rule, draft, game);
                out.push_back(Finish(std::move(rule), draft));
            }
            if (!draft.coupons.empty()) {
                Value rule = start(draft.uniques.empty() ? "" : " (Ancient Coupons)");
                rule.Set("itemCodes", StringList(draft.coupons));
                PutCommon(rule, draft, game);
                out.push_back(Finish(std::move(rule), draft));
            }
            break;
        }
        case ItemKind::Set: {
            Value rule = start("");
            rule.Set("quality", Value::String("set"));
            if (!draft.every) rule.Set("uniqueOrSetIds", IntList(draft.setItems));
            PutCommon(rule, draft, game);
            out.push_back(Finish(std::move(rule), draft));
            break;
        }
        case ItemKind::None: break;
    }
    return out;
}

auto FromRule(const Value& rule, const gamedata::GameData& game, RuleDraft& out) -> bool {
    out = RuleDraft{};
    if (!rule.IsObject()) return false;
    const auto truthy = [&rule](const char* key) {
        const Value* v = rule.Find(key);
        return v != nullptr && v->IsBool() && v->boolean;
    };
    const auto names = [](const Value* v) {
        std::vector<std::string> list;
        if (v == nullptr) return list;
        if (v->IsString()) list.push_back(v->Str());
        if (v->IsArray())
            for (const Value& e : v->array)
                if (e.IsString()) list.push_back(e.Str());
        return list;
    };
    const auto ints = [](const Value* v) {
        std::vector<int> list;
        if (v == nullptr || !v->IsArray()) return list;
        for (const Value& e : v->array) {
            std::int64_t n = 0;
            if (e.AsInteger(n)) list.push_back(static_cast<int>(n));
        }
        return list;
    };

    // The look, and what happens.
    Look& look = out.look;
    if (const Value* border = rule.Find("labelBorder"); border != nullptr && border->IsObject()) {
        Rgb c;
        if (ReadRgb(border->Find("color"), c)) look.border = c;
    }
    {
        Rgb c;
        if (ReadRgb(rule.Find("labelBackground"), c)) look.background = c;
    }
    if (const Value* prefix = rule.Find("namePrefix"); prefix != nullptr && prefix->IsString() && prefix->Str().size() >= 3) {
        const char code = prefix->Str().back();
        for (std::size_t i = 0; i < NameColors().size(); ++i)
            if (NameColors()[i].code == code) look.nameColor = static_cast<int>(i);
    }
    if (const Value* light = rule.Find("droplight"); light != nullptr && light->IsObject()) {
        look.beam = true;
        ReadRgb(light->Find("color"), look.beamColor);
    }
    if (const Value* sound = rule.Find("sound"); sound != nullptr && sound->IsString()) look.sound = sound->Str();
    if (const Value* icon = rule.Find("icon"); icon != nullptr && icon->IsObject()) {
        if (const Value* name = icon->Find("iconName"); name != nullptr && name->IsString()) look.icon = name->Str();
        std::int64_t size = look.iconSize;
        if (const Value* v = icon->Find("iconSize"); v != nullptr && v->AsInteger(size)) look.iconSize = static_cast<int>(size);
    }
    if (const Value* chat = rule.Find("chatMessage"); chat != nullptr && chat->IsString()) {
        look.chat = true;
        std::string text = chat->Str();
        if (text.rfind("\xC3\xBF" "c", 0) == 0 && text.size() >= 4) {
            for (std::size_t i = 0; i < NameColors().size(); ++i)
                if (NameColors()[i].code == text[3]) look.chatColor = static_cast<int>(i);
            text = text.substr(4);
        }
        look.chatText = text;
    }
    const bool hasLevels = rule.Find("activeInLevels") != nullptr;
    if (truthy("hide")) out.action = DraftAction::Hide;
    else if (truthy("show") || look.Any()) out.action = hasLevels ? DraftAction::Show : DraftAction::AlwaysShow;
    else return false;

    // The items.
    const std::vector<std::string> quality = names(rule.Find("quality"));
    const std::vector<std::string> codes = names(rule.Find("itemCodes"));
    const std::vector<int> ids = ints(rule.Find("uniqueOrSetIds"));
    const auto has = [&quality](const char* q) { return std::find(quality.begin(), quality.end(), q) != quality.end(); };
    if (!ids.empty() && has("unique")) {
        out.kind = ItemKind::Unique;
        out.uniques = ids;
    } else if (!ids.empty() && has("set")) {
        out.kind = ItemKind::Set;
        out.setItems = ids;
    } else if (!codes.empty() && std::all_of(codes.begin(), codes.end(), [&game](const std::string& c) { return IsCoupon(game, c); })) {
        out.kind = ItemKind::Unique;
        out.coupons = codes;
    } else if (!codes.empty() || rule.Find("itemTypes") != nullptr || has("magic") || has("rare")) {
        out.bases = codes;
        out.types = names(rule.Find("itemTypes"));
        out.every = codes.empty() && out.types.empty();  // magic or rare with nothing else: every one
        if (has("magic")) {
            out.kind = ItemKind::Magic;
        } else if (has("rare")) {
            out.kind = ItemKind::Rare;
        } else if (has("normal") || has("superior")) {
            out.kind = ItemKind::Base;
            if (has("normal")) out.baseQualities.insert(2);
            if (has("superior")) out.baseQualities.insert(3);
            if (out.baseQualities.size() == 2) out.baseQualities.clear();
        } else {
            out.kind = ItemKind::Other;
        }
        if (out.every && out.kind != ItemKind::Magic && out.kind != ItemKind::Rare) return false;
    } else if (has("unique")) {
        out.kind = ItemKind::Unique;
        out.every = true;
    } else if (has("set")) {
        out.kind = ItemKind::Set;
        out.every = true;
    } else {
        return false;
    }
    if (const Value* leave = rule.Find("excludeItemCodes"); leave != nullptr && leave->IsArray() && !leave->array.empty())
        out.mythical = RuleDraft::Mythicals::Leave;
    for (const std::string& t : names(rule.Find("baseTier")))
        for (int i = 1; i <= 3; ++i)
            if (t == kTierNames[i]) out.tiers.insert(static_cast<Tier>(i));
    if (const Value* checks = rule.Find("itemStats"); checks != nullptr && checks->IsArray()) {
        for (const Value& c : checks->array) {
            StatPick p;
            const Value* stat = c.Find("stat");
            if (stat == nullptr) continue;
            std::int64_t number = -1;
            if (stat->AsInteger(number)) p.stat = static_cast<int>(number);
            else if (stat->IsString()) p.stat = game.stats.Id(stat->Str());
            std::int64_t v = 0;
            if (const Value* param = c.Find("param"); param != nullptr && param->AsInteger(v)) p.param = static_cast<int>(v);
            if (const Value* value = c.Find("value"); value != nullptr && value->AsInteger(v)) p.atLeast = static_cast<int>(v);
            if (p.stat >= 0) out.stats.push_back(p);
        }
    }
    if (const Value* levels = rule.Find("activeInLevels"); levels != nullptr && levels->IsArray()) {
        for (const Value& l : levels->array) {
            std::int64_t id = 0;
            if (l.AsInteger(id)) out.levels.push_back(id);
        }
    }
    return true;
}

auto FromItem(const std::string& code, int quality, int uniqueOrSetId, const std::vector<ItemStat>& stats, const gamedata::GameData& game)
    -> RuleDraft {
    RuleDraft draft;
    const gamedata::Base* b = game.bases.Find(code);
    if (b == nullptr) return draft;
    if (IsCoupon(game, code)) {
        draft.kind = ItemKind::Unique;
        draft.coupons = {code};
        return draft;
    }
    if (quality == 7 && game.uniques.At(uniqueOrSetId) != nullptr) {
        draft.kind = ItemKind::Unique;
        draft.uniques = {uniqueOrSetId};
        return draft;
    }
    if (quality == 5 && game.sets.Item(uniqueOrSetId) != nullptr) {
        draft.kind = ItemKind::Set;
        draft.setItems = {uniqueOrSetId};
        return draft;
    }
    if (quality == 4 && b->rarities.magic) draft.kind = ItemKind::Magic;
    else if (quality == 6 && b->rarities.rare) draft.kind = ItemKind::Rare;
    else if (KindAllows(ItemKind::Base, game, *b)) draft.kind = ItemKind::Base;
    else draft.kind = ItemKind::Other;
    if (draft.kind == ItemKind::Base && (quality == 2 || quality == 3)) draft.baseQualities = {quality};
    draft.bases = {code};
    if (TierOf(*b) != Tier::None && draft.kind != ItemKind::Other) draft.tiers = {TierOf(*b)};
    const DraftOffer offer = Offer(draft, game);
    if (!offer.statsPossible) return draft;
    for (const ItemStat& s : stats) {
        for (const StatOption& o : offer.stats) {
            if (o.stat != s.stat || o.param != s.param || s.value <= 0) continue;
            draft.stats.push_back(StatPick{o.stat, o.param, std::clamp(s.value, o.min, o.max)});
        }
    }
    return draft;
}

}  // namespace esrfe::ui
