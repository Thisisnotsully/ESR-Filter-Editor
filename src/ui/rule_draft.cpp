#include "rule_draft.h"

#include <algorithm>
#include <map>

namespace esrfe::ui {

using document::Value;
using gamedata::Tier;

namespace {

const char* const kTierNames[] = {"", "normal", "exceptional", "elite"};  // by Tier
constexpr const char* kQualityNames[] = {"", "inferior", "normal", "superior", "magic", "set", "rare", "unique", "crafted", "tempered"};

auto TierOf(const gamedata::Base& b) -> Tier { return b.mythical ? Tier::Elite : b.tier; }

auto Comes(const gamedata::Base& b, int quality) -> bool {
    switch (quality) {
        case 2: return b.rarities.normal;
        case 3: return b.rarities.superior;
        case 4: return b.rarities.magic;
        case 5: return b.rarities.set;
        case 6: return b.rarities.rare;
        case 7: return b.rarities.unique;
        default: return false;
    }
}

// Stats whose param holds a skill and its level (chance to cast, charges) cannot be asked for "at least" sensibly.
auto Pickable(const gamedata::GameData& game, int stat, int param) -> bool {
    const gamedata::Stat* s = game.stats.At(stat);
    if (s == nullptr || param < 0) return false;
    return s->param != gamedata::ParamKind::SkillLevel;
}

auto Plain(const std::string& text) -> std::string { return gamedata::Strings::Plain(text); }

}  // namespace

auto HighlightColors() -> const std::vector<HighlightColor>& {
    static const std::vector<HighlightColor> colors{
        {"Gold", 255, 200, 0},  {"Red", 255, 60, 60},     {"Green", 60, 220, 90},
        {"Blue", 80, 140, 255}, {"Purple", 190, 90, 255}, {"White", 255, 255, 255},
    };
    return colors;
}

auto RarityChoices() -> const std::vector<std::pair<int, const char*>>& {
    static const std::vector<std::pair<int, const char*>> choices{
        {2, "Normal"}, {3, "Superior"}, {4, "Magic"}, {6, "Rare"}, {5, "Set"}, {7, "Unique"},
    };
    return choices;
}

auto ChosenBases(const RuleDraft& draft, const gamedata::GameData& game) -> std::vector<std::string> {
    std::vector<std::string> out;
    for (const std::string& code : draft.bases) {
        const gamedata::Base* b = game.bases.Find(code);
        if (b == nullptr) continue;
        if (draft.mythical == MythicalMode::Leave && b->mythical) continue;
        if (draft.mythical == MythicalMode::Only && !b->mythical) continue;
        if (!draft.tiers.empty() && TierOf(*b) != Tier::None && draft.tiers.count(TierOf(*b)) == 0) continue;
        out.push_back(code);
    }
    return out;
}

auto Offer(const RuleDraft& draft, const gamedata::GameData& game) -> DraftOffer {
    DraftOffer offer;
    for (const std::string& code : draft.bases) {
        const gamedata::Base* b = game.bases.Find(code);
        if (b == nullptr) continue;
        if (b->mythical) offer.mythicals = true;
        if (TierOf(*b) != Tier::None) offer.tierChoices.insert(TierOf(*b));
    }
    offer.tiers = offer.tierChoices.size() > 1 || (offer.tierChoices.size() == 1 && offer.mythicals);
    const std::vector<std::string> chosen = ChosenBases(draft, game);
    for (const std::string& code : chosen) {
        const gamedata::Base* b = game.bases.Find(code);
        for (const auto& [quality, name] : RarityChoices())
            if (Comes(*b, quality)) offer.rarities.insert(quality);
    }
    const bool superior = draft.rarities.size() == 1 && *draft.rarities.begin() == 3;
    const bool magic = draft.rarities.size() == 1 && *draft.rarities.begin() == 4;
    offer.statsPossible = (superior || magic) && !chosen.empty();
    if (!offer.statsPossible) {
        offer.statsWhy = offer.rarities.count(3) || offer.rarities.count(4)
                             ? "To filter by stats, choose only Superior or only Magic."
                             : "Stats are for Superior and Magic items, and these items do not come in either.";
        return offer;
    }
    const std::vector<gamedata::StatRange> ranges = superior ? game.SuperiorStatsFor(chosen) : game.MagicStatsFor(chosen);
    for (const gamedata::StatRange& r : ranges) {
        if (!Pickable(game, r.stat, r.param) || r.max <= 0) continue;
        StatOption o;
        o.stat = r.stat;
        o.param = r.param;
        o.min = std::max(1, r.min);  // a bonus that can also be a malus (bolts and crossbow damage): its positive part
        o.max = r.max;
        o.label = Plain(game.StatLine(r.stat, r.max, r.param, game.language));
        if (o.label.empty()) {
            const gamedata::Stat* s = game.stats.At(r.stat);
            o.label = (s != nullptr ? s->name : std::to_string(r.stat)) + " " + std::to_string(r.max);
        }
        offer.stats.push_back(std::move(o));
    }
    std::sort(offer.stats.begin(), offer.stats.end(), [](const StatOption& a, const StatOption& b) { return a.label < b.label; });
    return offer;
}

void Tidy(RuleDraft& draft, const gamedata::GameData& game) {
    const DraftOffer offer = Offer(draft, game);
    for (auto it = draft.rarities.begin(); it != draft.rarities.end();)
        it = offer.rarities.count(*it) ? std::next(it) : draft.rarities.erase(it);
    for (auto it = draft.tiers.begin(); it != draft.tiers.end();)
        it = offer.tierChoices.count(*it) ? std::next(it) : draft.tiers.erase(it);
    if (!offer.mythicals) draft.mythical = MythicalMode::Include;
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

auto ToRule(const RuleDraft& draft, const gamedata::GameData& game, const std::string& summary) -> edit::OwnRule {
    edit::OwnRule own;
    Value rule = Value::Object();
    const char* word = draft.action == DraftAction::Hide ? "hide" : draft.action == DraftAction::Show ? "show" : "highlight";
    rule.Set("comment", Value::String(summary + " | " + word));

    // Items: the bases picked (only the Mythicals, when only those).
    std::vector<std::string> codes;
    for (const std::string& code : draft.bases) {
        const gamedata::Base* b = game.bases.Find(code);
        if (draft.mythical == MythicalMode::Only && (b == nullptr || !b->mythical)) continue;
        codes.push_back(code);
    }
    Value list = Value::Array();
    for (const std::string& code : codes) list.array.push_back(Value::String(code));
    rule.Set("itemCodes", std::move(list));
    if (draft.mythical == MythicalMode::Leave) {
        Value leave = Value::Array();
        for (const std::string& code : codes) {
            const gamedata::Base* b = game.bases.Find(code);
            if (b != nullptr && b->mythical) leave.array.push_back(Value::String(code));
        }
        if (!leave.array.empty()) rule.Set("excludeItemCodes", std::move(leave));
    }
    const DraftOffer offer = Offer(draft, game);
    if (!draft.tiers.empty() && draft.tiers != offer.tierChoices) {
        Value tiers = Value::Array();
        for (Tier t : draft.tiers) tiers.array.push_back(Value::String(kTierNames[static_cast<int>(t)]));
        rule.Set("baseTier", tiers.array.size() == 1 ? tiers.array[0] : tiers);
    }
    if (!draft.rarities.empty() && draft.rarities != offer.rarities) {
        Value q = Value::Array();
        for (int quality : draft.rarities) q.array.push_back(Value::String(kQualityNames[quality]));
        rule.Set("quality", q.array.size() == 1 ? q.array[0] : q);
    }
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
    if (!draft.levels.empty()) {
        Value levels = Value::Array();
        for (std::int64_t id : draft.levels) levels.array.push_back(Value::Integer(id));
        rule.Set("activeInLevels", std::move(levels));
    }

    switch (draft.action) {
        case DraftAction::Hide: rule.Set("hide", Value::Bool(true)); own.place = "top"; break;
        case DraftAction::Show: rule.Set("show", Value::Bool(true)); own.place = "top"; break;
        default: {
            const HighlightColor& c = HighlightColors()[static_cast<std::size_t>(std::clamp<int>(draft.color, 0, static_cast<int>(HighlightColors().size()) - 1))];
            Value color = Value::Array({Value::Integer(c.r), Value::Integer(c.g), Value::Integer(c.b), Value::Integer(255)});
            rule.Set("labelBorder", Value::Object({{"color", color}, {"width", Value::Integer(2)}}));
            if (draft.beam) rule.Set("droplight", Value::Object({{"color", Value::Array({Value::Integer(c.r), Value::Integer(c.g), Value::Integer(c.b)})}}));
            if (draft.chat) rule.Set("notify", Value::Bool(true));
            own.place = "end";
            own.keepShown = true;
            break;
        }
    }
    own.rule = std::move(rule);
    return own;
}

auto FromRule(const Value& rule, const gamedata::GameData& game, RuleDraft& out) -> bool {
    out = RuleDraft{};
    if (!rule.IsObject()) return false;
    const auto truthy = [&rule](const char* key) {
        const Value* v = rule.Find(key);
        return v != nullptr && v->IsBool() && v->boolean;
    };
    if (truthy("hide")) out.action = DraftAction::Hide;
    else if (truthy("show")) out.action = DraftAction::Show;
    else if (const Value* border = rule.Find("labelBorder"); border != nullptr && border->IsObject()) {
        out.action = DraftAction::Highlight;
        if (const Value* color = border->Find("color"); color != nullptr && color->IsArray() && color->array.size() >= 3) {
            std::int64_t r = 0, g = 0, b = 0;
            color->array[0].AsInteger(r);
            color->array[1].AsInteger(g);
            color->array[2].AsInteger(b);
            const auto& colors = HighlightColors();
            for (std::size_t i = 0; i < colors.size(); ++i)
                if (colors[i].r == r && colors[i].g == g && colors[i].b == b) out.color = static_cast<int>(i);
        }
        out.beam = rule.Find("droplight") != nullptr;
        out.chat = truthy("notify");
    } else {
        return false;
    }
    const Value* codes = rule.Find("itemCodes");
    if (codes == nullptr || !codes->IsArray() || codes->array.empty()) return false;
    for (const Value& c : codes->array)
        if (c.IsString()) out.bases.push_back(c.Str());
    if (const Value* leave = rule.Find("excludeItemCodes"); leave != nullptr && leave->IsArray() && !leave->array.empty())
        out.mythical = MythicalMode::Leave;
    else if (std::all_of(out.bases.begin(), out.bases.end(), [&game](const std::string& c) {
                 const gamedata::Base* b = game.bases.Find(c);
                 return b != nullptr && b->mythical;
             }))
        out.mythical = MythicalMode::Include;  // only Mythicals were picked
    const auto names = [](const Value* v) {
        std::vector<std::string> list;
        if (v == nullptr) return list;
        if (v->IsString()) list.push_back(v->Str());
        if (v->IsArray())
            for (const Value& e : v->array)
                if (e.IsString()) list.push_back(e.Str());
        return list;
    };
    for (const std::string& t : names(rule.Find("baseTier")))
        for (int i = 1; i <= 3; ++i)
            if (t == kTierNames[i]) out.tiers.insert(static_cast<Tier>(i));
    for (const std::string& q : names(rule.Find("quality")))
        for (int i = 1; i <= 9; ++i)
            if (q == kQualityNames[i]) out.rarities.insert(i);
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

auto FromItem(const std::string& code, int quality, const std::vector<ItemStat>& stats, const gamedata::GameData& game) -> RuleDraft {
    RuleDraft draft;
    const gamedata::Base* b = game.bases.Find(code);
    if (b == nullptr) return draft;
    draft.bases = {code};
    if (TierOf(*b) != Tier::None) draft.tiers = {TierOf(*b)};
    if (quality >= 2 && quality <= 7) draft.rarities = {quality};
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

auto Complete(const RuleDraft& draft) -> bool { return draft.action != DraftAction::None && !draft.bases.empty(); }

}  // namespace esrfe::ui
