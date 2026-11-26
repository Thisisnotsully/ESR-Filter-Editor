#include "rule_scope.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "groups.h"
#include "ui_text.hpp"

namespace esrfe::ui {

using document::Value;
using overlay::UiFormat;
using overlay::UiText;

namespace {

// Lower-case first letter, for a word inside a sentence (English only: other languages keep theirs).
auto Lowered(std::string text) -> std::string {
    if (!text.empty() && text[0] >= 'A' && text[0] <= 'Z') text[0] = static_cast<char>(text[0] - 'A' + 'a');
    return text;
}

auto TierWord(gamedata::Tier t) -> std::string {
    switch (t) {
        case gamedata::Tier::Normal: return UiText("normal");
        case gamedata::Tier::Exceptional: return UiText("exceptional");
        case gamedata::Tier::Elite: return UiText("elite");
        default: return {};
    }
}

// A rune's family, by its code: ESR has the classic runes r51-r83 (El to Zod) and its own r01-r46 (I to Wo), each
// in rank order in the game's file; its other runes (r48, r50, r84-r97, nearly all level 60) have no rank, -1.
auto RuneFamily(const std::string& code) -> int {
    if (code.size() != 3 || code[0] != 'r' || !std::isdigit(static_cast<unsigned char>(code[1])) || !std::isdigit(static_cast<unsigned char>(code[2])))
        return -1;
    const int n = (code[1] - '0') * 10 + (code[2] - '0');
    if (n >= 51 && n <= 83) return 0;
    if (n >= 1 && n <= 46) return 1;
    return -1;
}

// The item types a weapons or armour group is, for "every ... weapon" without listing hundreds of codes.
auto GroupTypes(const std::string& groupId) -> std::vector<std::string> {
    if (groupId == "weapons") return {"weap", "misl"};
    if (groupId == "armor") return {"armo"};
    return {};
}

class Builder {
public:
    Builder(const RuleDraft& item, const gamedata::GameData& game, const DescribeContext& context)
        : item_(item), game_(game), context_(context) {}

    auto Build() -> std::vector<Scope> {
        switch (item_.kind) {
            case ItemKind::Base: Bases(); break;
            case ItemKind::Magic:
            case ItemKind::Rare: ByQuality(); break;
            case ItemKind::Unique: item_.coupons.empty() ? Uniques() : Coupons(); break;
            case ItemKind::Set: Sets(); break;
            case ItemKind::Other: Others(); break;
            case ItemKind::None: break;
        }
        return std::move(out_);
    }

private:
    auto BaseName(const std::string& code) const -> std::string { return EntryName("itemCodes", Value::String(code), context_); }
    auto Code() const -> std::string { return item_.bases.empty() ? std::string{} : item_.bases.front(); }
    auto Where() const -> gamedata::GroupRef { return game_.groups.Of(Code()); }
    auto GroupAt(const gamedata::GroupRef& r) const -> const gamedata::Group* {
        return r.group >= 0 && r.group < static_cast<int>(game_.groups.All().size()) ? &game_.groups.All()[static_cast<std::size_t>(r.group)] : nullptr;
    }
    auto SubgroupAt(const gamedata::GroupRef& r) const -> const gamedata::Subgroup* {
        const gamedata::Group* g = GroupAt(r);
        return g != nullptr && r.subgroup >= 0 && r.subgroup < static_cast<int>(g->subgroups.size()) ? &g->subgroups[static_cast<std::size_t>(r.subgroup)] : nullptr;
    }
    // A draft of the item's kind with nothing chosen yet (its quality kept).
    auto Empty() const -> RuleDraft {
        RuleDraft d;
        d.kind = item_.kind;
        d.baseQualities = item_.baseQualities;
        return d;
    }
    auto Allowed(const std::vector<std::string>& codes) const -> std::vector<std::string> {
        std::vector<std::string> out;
        for (const std::string& c : codes)
            if (const gamedata::Base* b = game_.bases.Find(c); b != nullptr && KindAllows(item_.kind, game_, *b)) out.push_back(c);
        return out;
    }
    // Added unless it is the same items as the one before (a list of one, a subgroup that is the whole group).
    void Add(std::string label, RuleDraft draft, std::size_t count) {
        if (!out_.empty()) {
            const RuleDraft& last = out_.back().draft;
            std::vector<std::string> a = last.bases, b = draft.bases;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            std::vector<int> u = last.uniques, v = draft.uniques;
            std::sort(u.begin(), u.end());
            std::sort(v.begin(), v.end());
            std::vector<int> s = last.setItems, t = draft.setItems;
            std::sort(s.begin(), s.end());
            std::sort(t.begin(), t.end());
            if (a == b && u == v && s == t && last.types == draft.types && last.every == draft.every && last.tiers == draft.tiers &&
                last.stats.size() == draft.stats.size() && last.coupons.size() == draft.coupons.size())
                return;
        }
        out_.push_back(Scope{std::move(label), std::move(draft), count});
    }

    // White and superior weapons and armour.
    void Bases() {
        const std::string code = Code();
        const std::string name = BaseName(code);
        const bool superior = item_.baseQualities == std::set<int>{3};
        const std::string quality = superior ? UiText("superior") : UiText("white");
        if (!item_.stats.empty()) {  // a superior item's stats: the narrowest
            std::vector<std::string> stats;
            const DraftOffer offer = Offer(item_, game_);
            for (const StatPick& p : item_.stats)
                for (const StatOption& o : offer.stats)
                    if (o.stat == p.stat && o.param == p.param) stats.push_back(o.label);
            RuleDraft d = item_;
            Add(UiFormat("Every superior {0} with {1} or more", {name, JoinNames(stats, 2)}), d, 1);
        }
        RuleDraft one = Empty();
        one.bases = {code};
        Add(UiFormat("Every {0} {1}", {quality, name}), one, 1);
        const gamedata::GroupRef where = Where();
        if (const gamedata::Subgroup* sub = SubgroupAt(where)) {
            RuleDraft d = Empty();
            d.bases = Allowed(sub->bases);
            const std::size_t n = d.bases.size();
            Add(UiFormat("Every {0} item in {1}", {quality, gamedata::Groups::Name(*sub, game_.strings, context_.language)}), d, n);
        }
        if (const gamedata::Group* group = GroupAt(where); group != nullptr && !GroupTypes(group->id).empty()) {
            const std::string groupName = gamedata::Groups::Name(*group, game_.strings, context_.language);
            const gamedata::Base* b = game_.bases.Find(code);
            if (b != nullptr && b->tier != gamedata::Tier::None) {
                RuleDraft d = Empty();
                d.types = GroupTypes(group->id);
                d.tiers = {b->tier};
                Add(UiFormat("Every {0} {1}-tier item in {2}", {quality, TierWord(b->tier), groupName}), d, 0);
            }
            RuleDraft all = Empty();
            all.types = GroupTypes(group->id);
            all.baseQualities.clear();
            Add(UiFormat("Every white and superior item in {0}", {groupName}), all, 0);
        }
    }

    // Magic and rare items.
    void ByQuality() {
        const std::string code = Code();
        const std::string word = item_.kind == ItemKind::Magic ? UiText("magic") : UiText("rare");
        if (!item_.stats.empty()) {
            RuleDraft d = item_;
            Add(UiFormat("Every {0} {1} with these stats", {word, BaseName(code)}), d, 1);
        }
        RuleDraft one = Empty();
        one.bases = {code};
        Add(UiFormat("Every {0} {1}", {word, BaseName(code)}), one, 1);
        const gamedata::GroupRef where = Where();
        if (const gamedata::Subgroup* sub = SubgroupAt(where)) {
            RuleDraft d = Empty();
            d.bases = Allowed(sub->bases);
            const std::size_t n = d.bases.size();
            Add(UiFormat("Every {0} item in {1}", {word, gamedata::Groups::Name(*sub, game_.strings, context_.language)}), d, n);
        }
        if (const gamedata::Group* group = GroupAt(where)) {
            RuleDraft d = Empty();
            if (!GroupTypes(group->id).empty()) {
                d.types = GroupTypes(group->id);
            } else {
                for (const gamedata::Subgroup& s : group->subgroups)
                    for (const std::string& c : Allowed(s.bases)) d.bases.push_back(c);
            }
            const std::size_t n = d.bases.size();
            Add(UiFormat("Every {0} item in {1}", {word, gamedata::Groups::Name(*group, game_.strings, context_.language)}), d, n);
        }
        RuleDraft every = Empty();
        every.every = true;
        Add(UiFormat("Every {0} item", {word}), every, 0);
    }

    void Uniques() {
        if (item_.uniques.empty()) return;
        const gamedata::Unique* u = game_.uniques.At(item_.uniques.front());
        if (u == nullptr) return;
        const auto uniqueName = [this](const gamedata::Unique& x) {
            return gamedata::Strings::Plain(game_.uniques.Name(x, game_.strings, context_.language));
        };
        RuleDraft one = Empty();
        one.uniques = {u->row};
        Add(UiFormat("Only {0}", {uniqueName(*u)}), one, 1);
        const auto collect = [this](auto&& keep) {
            std::vector<int> rows;
            for (const gamedata::Unique& x : game_.uniques.All())
                if (!x.disabled && keep(x)) rows.push_back(x.row);
            return rows;
        };
        {
            RuleDraft d = Empty();
            d.uniques = collect([&](const gamedata::Unique& x) { return x.code == u->code; });
            const std::size_t n = d.uniques.size();
            Add(UiFormat("Every unique {0}", {BaseName(u->code)}), d, n);
        }
        const gamedata::GroupRef where = game_.groups.Of(u->code);
        if (const gamedata::Subgroup* sub = SubgroupAt(where)) {
            const std::set<std::string> codes(sub->bases.begin(), sub->bases.end());
            RuleDraft d = Empty();
            d.uniques = collect([&](const gamedata::Unique& x) { return codes.count(x.code) != 0; });
            const std::size_t n = d.uniques.size();
            Add(UiFormat("Every unique in {0}", {gamedata::Groups::Name(*sub, game_.strings, context_.language)}), d, n);
        }
        const gamedata::Tier tier = TierOfUnique(game_, u->code);
        {
            RuleDraft d = Empty();
            d.uniques = collect([&](const gamedata::Unique& x) { return TierOfUnique(game_, x.code) == tier; });
            const std::size_t n = d.uniques.size();
            Add(tier == gamedata::Tier::None ? UiText("Every unique ring, amulet, charm and jewel")
                                             : UiFormat("Every {0}-tier unique", {TierWord(tier)}),
                d, n);
        }
        RuleDraft every = Empty();
        every.every = true;
        Add(UiText("Every unique"), every, 0);
    }

    void Coupons() {
        const std::string code = item_.coupons.front();
        RuleDraft one = Empty();
        one.coupons = {code};
        Add(UiFormat("Only {0}", {BaseName(code)}), one, 1);
        gamedata::Tier tier = gamedata::Tier::None;
        for (const Coupon& c : ui::Coupons(game_))
            if (c.code == code) tier = c.tier;
        if (tier != gamedata::Tier::None) {
            RuleDraft d = Empty();
            for (const Coupon& c : ui::Coupons(game_))
                if (c.tier == tier) d.coupons.push_back(c.code);
            const std::size_t n = d.coupons.size();
            Add(UiFormat("Every {0}-tier Ancient Coupon", {TierWord(tier)}), d, n);
        }
        RuleDraft all = Empty();
        for (const Coupon& c : ui::Coupons(game_)) all.coupons.push_back(c.code);
        const std::size_t n = all.coupons.size();
        Add(UiText("Every Ancient Coupon"), all, n);
    }

    void Sets() {
        if (item_.setItems.empty()) return;
        const gamedata::SetItem* item = game_.sets.Item(item_.setItems.front());
        if (item == nullptr) return;
        RuleDraft one = Empty();
        one.setItems = {item->row};
        Add(UiFormat("Only {0}", {gamedata::Strings::Plain(game_.sets.Name(*item, game_.strings, context_.language))}), one, 1);
        if (const gamedata::Set* set = game_.sets.SetOf(*item)) {
            RuleDraft d = Empty();
            for (int row : set->items)
                if (const gamedata::SetItem* x = game_.sets.Item(row); x != nullptr && !x->disabled) d.setItems.push_back(row);
            const std::size_t n = d.setItems.size();
            Add(UiFormat("The whole {0} set", {gamedata::Strings::Plain(game_.sets.SetName(*set, game_.strings, context_.language))}), d, n);
        }
        {  // set items of the same tier, as uniques
            const gamedata::Tier tier = TierOfUnique(game_, item->code);
            RuleDraft d = Empty();
            for (const gamedata::SetItem& x : game_.sets.Items())
                if (!x.disabled && TierOfUnique(game_, x.code) == tier) d.setItems.push_back(x.row);
            const std::size_t n = d.setItems.size();
            Add(tier == gamedata::Tier::None ? UiText("Every set ring, amulet, charm and jewel") : UiFormat("Every {0}-tier set item", {TierWord(tier)}), d, n);
        }
        RuleDraft every = Empty();
        every.every = true;
        Add(UiText("Every set item"), every, 0);
    }

    // Runes, gems, potions, keys and the like.
    void Others() {
        const std::string code = Code();
        RuleDraft one = Empty();
        one.bases = {code};
        Add(UiFormat("Only {0}", {BaseName(code)}), one, 1);
        const gamedata::GroupRef where = Where();
        const gamedata::Subgroup* sub = SubgroupAt(where);
        if (sub != nullptr && RuneFamily(code) >= 0) {  // the rune and those after it in its family (the game's order)
            RuleDraft d = Empty();
            bool from = false;
            for (const std::string& c : Allowed(sub->bases)) {
                from = from || c == code;
                if (from && RuneFamily(c) == RuneFamily(code)) d.bases.push_back(c);
            }
            const std::size_t n = d.bases.size();
            if (n > 1) Add(UiFormat("{0} and every higher rune", {BaseName(code)}), d, n);
        }
        if (const char color = RuneColor(game_, code); sub != nullptr && color != 0) {  // ESR runes go by colour
            RuleDraft d = Empty();
            for (const std::string& c : Allowed(sub->bases))
                if (RuneColor(game_, c) == color) d.bases.push_back(c);
            const std::size_t n = d.bases.size();
            Add(color == 'N' ? UiText("Every Elemental Rune") : UiFormat("Every {0} rune", {Lowered(UiText(RuneColorName(color)))}), d, n);
        } else if (sub != nullptr && RuneFamily(code) == 0) {  // the classic runes, El to Zod
            RuleDraft d = Empty();
            for (const std::string& c : Allowed(sub->bases))
                if (RuneFamily(c) == 0) d.bases.push_back(c);
            const std::size_t n = d.bases.size();
            Add(UiText("Every classic rune (El to Zod)"), d, n);
        }
        if (sub != nullptr) {
            RuleDraft d = Empty();
            d.bases = Allowed(sub->bases);
            const std::size_t n = d.bases.size();
            Add(UiFormat("Every item in {0}", {gamedata::Groups::Name(*sub, game_.strings, context_.language)}), d, n);
        }
        if (const gamedata::Group* group = GroupAt(where); group != nullptr && group->subgroups.size() > 1) {
            RuleDraft d = Empty();
            for (const gamedata::Subgroup& s : group->subgroups)
                for (const std::string& c : Allowed(s.bases)) d.bases.push_back(c);
            const std::size_t n = d.bases.size();
            Add(UiFormat("Every item in {0}", {gamedata::Groups::Name(*group, game_.strings, context_.language)}), d, n);
        }
    }

    RuleDraft item_;
    const gamedata::GameData& game_;
    const DescribeContext& context_;
    std::vector<Scope> out_;
};

}  // namespace

auto ScopesFor(const RuleDraft& item, const gamedata::GameData& game, const DescribeContext& context) -> std::vector<Scope> {
    return Builder(item, game, context).Build();
}

}  // namespace esrfe::ui
