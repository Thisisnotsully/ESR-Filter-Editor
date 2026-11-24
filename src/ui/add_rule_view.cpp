#include "add_rule_view.hpp"

#include <algorithm>
#include <initializer_list>

#include "groups.h"
#include "text_fold.hpp"
#include "ui_text.hpp"

namespace esrfe::ui {

using namespace overlay::text_flags;
using document::Value;
using overlay::Font;
using overlay::UiFormat;
using overlay::UiText;

namespace {

constexpr const char* kStepNames[] = {"1 What happens", "2 Items", "3 Tier", "4 Rarity", "5 Stats", "6 Levels", "7 Check"};
constexpr const char* kStepHeadings[] = {
    "What should happen to these items?",
    "Which items?",
    "Which tiers?",
    "Which rarities?",
    "Which stats?",
    "On which filter levels?",
    "Your rule",
};
constexpr const char* kStepHints[] = {
    "Hide them, always show them (no rule can hide them then), or highlight them with a coloured border.",
    "Choose a group on the left, then tick items on the right. Or search for an item by name.",
    "Normal, Exceptional, Elite. Mythical bases count as Elite; you can leave them out or keep only them.",
    "Leave every box empty for every rarity.",
    "Tick the bonuses the item must have and how much. Rules with stats only work on identified items.",
    "Leave every box ticked for every level.",
    "Check it, then add it. Your own rules come first in the filter, so they win over the author's.",
};
const char* const kTierLabels[] = {"Normal", "Exceptional", "Elite"};
const gamedata::Tier kTiers[] = {gamedata::Tier::Normal, gamedata::Tier::Exceptional, gamedata::Tier::Elite};
const char* const kMythLabels[] = {"With Mythicals", "Without Mythicals", "Only Mythicals"};

auto TierWord(const gamedata::Base& b) -> const char* {
    if (b.mythical) return "Mythical";
    switch (b.tier) {
        case gamedata::Tier::Normal: return "Normal";
        case gamedata::Tier::Exceptional: return "Exceptional";
        case gamedata::Tier::Elite: return "Elite";
        default: return "";
    }
}

auto Has(const std::vector<std::string>& list, const std::string& code) -> bool {
    return std::find(list.begin(), list.end(), code) != list.end();
}

}  // namespace

AddRuleView::AddRuleView(std::shared_ptr<Loaded> loaded, RuleDraft draft, std::string editingId)
    : loaded_(std::move(loaded)), game_(*loaded_->game), draft_(std::move(draft)), editing_(std::move(editingId)) {
    for (int i = 0; i < kSteps; ++i) {
        overlay::Chip& chip = stepChips_.emplace_back();
        chip.label = kStepNames[i];
        chip.onClick = [this, i](Ui& ui) {
            GoTo(static_cast<Step>(i));
            ui.Repaint();
        };
    }
    heading_.font = Font::Bold;
    heading_.useThemeColor = false;
    hint_.font = Font::Small;
    hint_.flags = kLeft;
    hint_.useThemeColor = false;

    // 1 What happens.
    hide_.label = "Hide them";
    show_.label = "Always show them";
    highlight_.label = "Highlight them";
    for (overlay::Chip* c : {&hide_, &show_, &highlight_}) c->font = Font::Body;
    hide_.onClick = [this](Ui& ui) { draft_.action = DraftAction::Hide; Changed(); ui.Repaint(); };
    show_.onClick = [this](Ui& ui) { draft_.action = DraftAction::Show; Changed(); ui.Repaint(); };
    highlight_.onClick = [this](Ui& ui) { draft_.action = DraftAction::Highlight; Changed(); ui.Repaint(); };
    colorLabel_.text = "Border colour";
    colorLabel_.font = Font::Small;
    for (std::size_t i = 0; i < HighlightColors().size(); ++i) {
        overlay::Chip& chip = colors_.emplace_back();
        chip.label = HighlightColors()[i].english;
        chip.onClick = [this, i](Ui& ui) { draft_.color = static_cast<int>(i); Changed(); ui.Repaint(); };
    }
    beam_.label = "A light beam where it drops";
    beam_.onChange = [this](bool on, Ui& ui) { draft_.beam = on; Changed(); ui.Repaint(); };
    chat_.label = "A chat line when it drops";
    chat_.onChange = [this](bool on, Ui& ui) { draft_.chat = on; Changed(); ui.Repaint(); };

    // 2 Items.
    itemSearch_.search = true;
    itemSearch_.placeholder = "Search for an item: Mystic Staff, Ber Rune, ring…";
    itemSearch_.onChange = [this](const std::string&, Ui& ui) { RebuildBases(); baseList_.SetScroll(0); ui.Repaint(); };
    for (std::size_t g = 0; g < game_.groups.All().size(); ++g) {
        const gamedata::Group& group = game_.groups.All()[g];
        const std::string name = gamedata::Groups::Name(group, game_.strings, loaded_->language);
        if (group.subgroups.size() == 1) {
            groupRows_.push_back(GroupRow{static_cast<int>(g), 0, name});
            continue;
        }
        groupRows_.push_back(GroupRow{static_cast<int>(g), -1, name});
        for (std::size_t s = 0; s < group.subgroups.size(); ++s)
            groupRows_.push_back(GroupRow{static_cast<int>(g), static_cast<int>(s),
                                          gamedata::Groups::Name(group.subgroups[s], game_.strings, loaded_->language)});
    }
    groupList_.rowGap = 0;
    groupList_.SetCount(static_cast<int>(groupRows_.size()));
    groupList_.paintRow = [this](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(groupRows_.size())) return;
        const GroupRow& r = groupRows_[static_cast<std::size_t>(index)];
        const overlay::Theme& theme = canvas.GetTheme();
        const bool heading = r.subgroup < 0;
        const bool single = !heading && game_.groups.All()[static_cast<std::size_t>(r.group)].subgroups.size() == 1;
        const Box text{row.left + canvas.S(heading || single ? 8 : 22), row.top, row.right - canvas.S(6), row.bottom};
        canvas.PushClip(text);
        canvas.Text(r.name, text, heading ? theme.headerGold : theme.text, heading || single ? Font::Bold : Font::Body, kVCenter | kSingleLine);
        canvas.PopClip();
    };
    groupList_.onSelect = [this](int, Ui& ui) {
        itemSearch_.SetValue("");
        RebuildBases();
        baseList_.SetScroll(0);
        ui.Repaint();
    };
    baseList_.rowGap = 0;
    baseList_.emptyText = "Choose a group on the left";
    baseList_.paintRow = [this](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(baseRows_.size())) return;
        const std::string& code = baseRows_[static_cast<std::size_t>(index)];
        const gamedata::Base* b = game_.bases.Find(code);
        const overlay::Theme& theme = canvas.GetTheme();
        const bool on = Has(draft_.bases, code);
        const int size = canvas.S(14);
        const Box tick{row.left + canvas.S(8), row.CentreY() - size / 2, row.left + canvas.S(8) + size, row.CentreY() - size / 2 + size};
        canvas.Fill(tick, theme.field);
        canvas.Frame(tick, theme.border);
        if (on) canvas.Fill(tick.Inflated(-canvas.S(3)), theme.gold);
        const std::string tier = b != nullptr ? UiText(TierWord(*b)) : std::string{};
        const int tierWidth = tier.empty() ? 0 : canvas.TextWidth(tier, Font::Small) + canvas.S(10);
        const Box text{tick.right + canvas.S(8), row.top, row.right - tierWidth - canvas.S(6), row.bottom};
        canvas.PushClip(text);
        canvas.Text(EntryName("itemCodes", Value::String(code), loaded_->context), text, on ? theme.text : theme.dim, Font::Body,
                    kVCenter | kSingleLine);
        canvas.PopClip();
        if (!tier.empty())
            canvas.Text(tier, Box{row.right - tierWidth, row.top, row.right - canvas.S(6), row.bottom}, theme.dim, Font::Small,
                        kRight | kVCenter | kSingleLine);
    };
    baseList_.onClick = [this](int index, Ui& ui) { ToggleBase(index, ui); };
    baseList_.onActivate = [this](int index, Ui& ui) { ToggleBase(index, ui); };
    all_.label = "Tick all";
    all_.onClick = [this](Ui& ui) {
        for (const std::string& code : baseRows_)
            if (!Has(draft_.bases, code)) draft_.bases.push_back(code);
        Changed();
        ui.Repaint();
    };
    none_.label = "Untick all";
    none_.onClick = [this](Ui& ui) {
        draft_.bases.erase(std::remove_if(draft_.bases.begin(), draft_.bases.end(), [this](const std::string& c) { return Has(baseRows_, c); }),
                           draft_.bases.end());
        Changed();
        ui.Repaint();
    };
    chosen_.translate = false;
    chosen_.font = Font::Small;
    chosen_.useThemeColor = false;

    // 3 Tier.
    for (std::size_t i = 0; i < 3; ++i) {
        overlay::Checkbox& box = tiers_.emplace_back();
        box.label = kTierLabels[i];
        box.onChange = [this, i](bool on, Ui& ui) {
            const DraftOffer offer = Offer(draft_, game_);
            if (draft_.tiers.empty()) draft_.tiers = offer.tierChoices;  // every tier was meant: now all but this one
            if (on) draft_.tiers.insert(kTiers[i]);
            else draft_.tiers.erase(kTiers[i]);
            if (draft_.tiers == offer.tierChoices) draft_.tiers.clear();
            Changed();
            ui.Repaint();
        };
    }
    mythLabel_.text = "Mythical bases";
    mythLabel_.font = Font::Small;
    for (std::size_t i = 0; i < 3; ++i) {
        overlay::Chip& chip = myth_.emplace_back();
        chip.label = kMythLabels[i];
        chip.onClick = [this, i](Ui& ui) { draft_.mythical = static_cast<MythicalMode>(i); Changed(); ui.Repaint(); };
    }

    // 4 Rarity.
    for (const auto& [quality, name] : RarityChoices()) {
        overlay::Checkbox& box = rarities_.emplace_back();
        box.label = name;
        const int q = quality;
        box.onChange = [this, q](bool on, Ui& ui) {
            if (on) draft_.rarities.insert(q);
            else draft_.rarities.erase(q);
            Changed();
            ui.Repaint();
        };
    }

    // 5 Stats.
    statSearch_.search = true;
    statSearch_.placeholder = "Search the bonuses: life, skills, resist…";
    statSearch_.onChange = [this](const std::string&, Ui& ui) { RebuildStats(); statList_.SetScroll(0); ui.Repaint(); };
    statList_.rowGap = 0;
    statList_.emptyText = "No bonuses";
    statList_.paintRow = [this](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(statRows_.size())) return;
        const StatOption& o = offer_.stats[static_cast<std::size_t>(statRows_[static_cast<std::size_t>(index)])];
        const overlay::Theme& theme = canvas.GetTheme();
        const StatPick* pick = nullptr;
        for (const StatPick& p : draft_.stats)
            if (p.stat == o.stat && p.param == o.param) pick = &p;
        const int size = canvas.S(14);
        const Box tick{row.left + canvas.S(8), row.CentreY() - size / 2, row.left + canvas.S(8) + size, row.CentreY() - size / 2 + size};
        canvas.Fill(tick, theme.field);
        canvas.Frame(tick, theme.border);
        if (pick != nullptr) canvas.Fill(tick.Inflated(-canvas.S(3)), theme.gold);
        const std::string right = pick != nullptr ? UiFormat("at least {0}", {std::to_string(pick->atLeast)})
                                                  : UiFormat("{0} to {1}", {std::to_string(o.min), std::to_string(o.max)});
        const int rightWidth = canvas.TextWidth(right, Font::Small) + canvas.S(12);
        const Box text{tick.right + canvas.S(8), row.top, row.right - rightWidth, row.bottom};
        canvas.PushClip(text);
        canvas.Text(o.label, text, pick != nullptr ? theme.text : theme.dim, Font::Body, kVCenter | kSingleLine);
        canvas.PopClip();
        canvas.Text(right, Box{row.right - rightWidth, row.top, row.right - canvas.S(6), row.bottom}, pick != nullptr ? theme.gold : theme.dim,
                    Font::Small, kRight | kVCenter | kSingleLine);
    };
    statList_.onClick = [this](int index, Ui& ui) { ToggleStat(index, ui); };
    statList_.onActivate = [this](int index, Ui& ui) { ToggleStat(index, ui); };
    statList_.onSelect = [this](int, Ui& ui) { SyncControls(); ui.Repaint(); };
    atLeastLabel_.text = "At least";
    atLeastLabel_.font = Font::Small;
    atLeast_.onChange = [this](int value, Ui& ui) {
        const int row = statList_.Selected();
        if (row < 0 || row >= static_cast<int>(statRows_.size())) return;
        const StatOption& o = offer_.stats[static_cast<std::size_t>(statRows_[static_cast<std::size_t>(row)])];
        for (StatPick& p : draft_.stats)
            if (p.stat == o.stat && p.param == o.param) p.atLeast = value;
        Changed();
        ui.Repaint();
    };

    // 6 Levels.
    for (const document::FilterLevel& l : loaded_->doc.GetSettings().filterLevels) {
        levelIds_.push_back(l.id);
        overlay::Checkbox& box = levels_.emplace_back();
        box.label = PickLanguage(l.name, loaded_->language);
        box.translate = false;
        const std::size_t index = levels_.size() - 1;
        box.onChange = [this, index](bool on, Ui& ui) {
            std::vector<std::int64_t> chosen;
            for (std::size_t i = 0; i < levels_.size(); ++i)
                if (i == index ? on : levels_[i].on) chosen.push_back(levelIds_[i]);
            if (chosen.empty()) {
                levels_[index].on = true;
                status_.text = UiText("A rule needs at least one level.");
                ui.Repaint();
                return;
            }
            draft_.levels = chosen.size() == levelIds_.size() ? std::vector<std::int64_t>{} : chosen;
            Changed();
            ui.Repaint();
        };
    }

    // 7 Check.
    checkLines_.drawRowBackground = false;
    checkLines_.rowGap = 0;
    checkLines_.paintRow = [this](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(checkText_.size())) return;
        const std::string& line = checkText_[static_cast<std::size_t>(index)];
        const overlay::Theme& theme = canvas.GetTheme();
        const bool heading = !line.empty() && line[0] == '#';
        const Box text{row.left + canvas.S(heading ? 8 : 24), row.top, row.right - canvas.S(8), row.bottom};
        canvas.PushClip(text);
        canvas.Text(heading ? line.substr(1) : line, text, heading ? theme.headerGold : theme.text, heading ? Font::Bold : Font::Body,
                    kVCenter | kSingleLine);
        canvas.PopClip();
    };

    summary_.translate = false;
    summary_.font = Font::Small;
    summary_.useThemeColor = false;
    status_.translate = false;
    status_.font = Font::Small;
    status_.useThemeColor = false;
    back_.label = "< Back";
    back_.onClick = [this](Ui& ui) {
        int i = static_cast<int>(step_) - 1;
        while (i >= 0 && !StepUsed(static_cast<Step>(i))) --i;
        if (i < 0) {
            ui.PopView();
            return;
        }
        GoTo(static_cast<Step>(i));
        ui.Repaint();
    };
    next_.label = "Next >";
    next_.onClick = [this](Ui& ui) {
        int i = static_cast<int>(step_) + 1;
        while (i < kSteps && !StepUsed(static_cast<Step>(i))) ++i;
        if (i < kSteps) GoTo(static_cast<Step>(i));
        ui.Repaint();
    };
    finish_.label = editing_.empty() ? "Add rule" : "Save rule";
    finish_.style = overlay::Button::Style::Primary;
    finish_.onClick = [this](Ui& ui) { Save(ui); };

    // Start on the group of the first item (the hover key's item).
    if (!draft_.bases.empty()) {
        const gamedata::GroupRef ref = game_.groups.Of(draft_.bases.front());
        for (std::size_t r = 0; r < groupRows_.size(); ++r)
            if (groupRows_[r].group == ref.group && groupRows_[r].subgroup == std::max(ref.subgroup, 0)) groupList_.Select(static_cast<int>(r), nullptr);
    }
    RebuildBases();
    Changed();
    GoTo(Step::Action);
}

auto AddRuleView::Title() const -> std::string {
    return editing_.empty() ? UiText("New rule") : UiFormat("Change your rule {0}", {editing_});
}

auto AddRuleView::GroupRowName(int row) const -> std::string {
    return row >= 0 && row < static_cast<int>(groupRows_.size()) ? groupRows_[static_cast<std::size_t>(row)].name : std::string{};
}

auto AddRuleView::StepUsed(Step step) const -> bool {
    switch (step) {
        case Step::Tier: return offer_.tiers || offer_.mythicals;
        case Step::Rarity: return offer_.rarities.size() > 1;
        case Step::Stats: return offer_.rarities.count(3) != 0 || offer_.rarities.count(4) != 0;
        case Step::Levels: return !levelIds_.empty();
        default: return true;
    }
}

void AddRuleView::GoTo(Step step) {
    step_ = step;
    if (step == Step::Stats) RebuildStats();
    ShowStep();
    SyncControls();
}

void AddRuleView::RebuildBases() {
    baseRows_.clear();
    const std::string query = itemSearch_.Value();
    if (!query.empty()) {
        const overlay::text::ParsedQuery parsed = overlay::text::ParseQuery(query);
        for (const gamedata::Group& g : game_.groups.All())
            for (const gamedata::Subgroup& s : g.subgroups)
                for (const std::string& code : s.bases) {
                    const std::string name = EntryName("itemCodes", Value::String(code), loaded_->context);
                    if (overlay::text::Matches(parsed, overlay::text::SearchTextUtf8(name + "\n" + code)) && !Has(baseRows_, code))
                        baseRows_.push_back(code);
                }
    } else {
        const int row = groupList_.Selected();
        if (row >= 0 && row < static_cast<int>(groupRows_.size())) {
            const GroupRow& r = groupRows_[static_cast<std::size_t>(row)];
            const gamedata::Group& g = game_.groups.All()[static_cast<std::size_t>(r.group)];
            if (r.subgroup >= 0) baseRows_ = g.subgroups[static_cast<std::size_t>(r.subgroup)].bases;
            else
                for (const gamedata::Subgroup& s : g.subgroups) baseRows_.insert(baseRows_.end(), s.bases.begin(), s.bases.end());
        }
    }
    baseList_.SetCount(static_cast<int>(baseRows_.size()));
}

void AddRuleView::RebuildStats() {
    statRows_.clear();
    const overlay::text::ParsedQuery parsed = overlay::text::ParseQuery(statSearch_.Value());
    for (std::size_t i = 0; i < offer_.stats.size(); ++i)
        if (parsed.empty() || overlay::text::Matches(parsed, overlay::text::SearchTextUtf8(offer_.stats[i].label))) statRows_.push_back(static_cast<int>(i));
    statList_.SetCount(static_cast<int>(statRows_.size()));
}

void AddRuleView::ToggleBase(int row, Ui& ui) {
    if (row < 0 || row >= static_cast<int>(baseRows_.size())) return;
    const std::string& code = baseRows_[static_cast<std::size_t>(row)];
    const auto it = std::find(draft_.bases.begin(), draft_.bases.end(), code);
    if (it != draft_.bases.end()) draft_.bases.erase(it);
    else draft_.bases.push_back(code);
    Changed();
    ui.Repaint();
}

void AddRuleView::ToggleStat(int row, Ui& ui) {
    if (row < 0 || row >= static_cast<int>(statRows_.size())) return;
    const StatOption& o = offer_.stats[static_cast<std::size_t>(statRows_[static_cast<std::size_t>(row)])];
    const auto it = std::find_if(draft_.stats.begin(), draft_.stats.end(), [&](const StatPick& p) { return p.stat == o.stat && p.param == o.param; });
    if (it != draft_.stats.end()) draft_.stats.erase(it);
    else draft_.stats.push_back(StatPick{o.stat, o.param, o.min});
    statList_.Select(row, nullptr);
    Changed();
    ui.Repaint();
}

auto AddRuleView::RuleNow() const -> edit::OwnRule {
    // The title: what the rule is about and when, as the list says it.
    edit::OwnRule draftRule = ToRule(draft_, game_, "x");
    const RuleSummary s = Describe(draftRule.rule, loaded_->context);
    std::string title = overlay::StripColorCodes(s.what);
    for (const std::string& c : s.conditions) title += ", " + c;
    if (title.size() > 120) title = title.substr(0, 117) + "...";
    return ToRule(draft_, game_, title);
}

auto AddRuleView::Summary() const -> std::string {
    if (!Complete(draft_)) {
        if (draft_.action == DraftAction::None) return UiText("Choose what should happen.");
        return UiText("Choose at least one item.");
    }
    std::string line = summaryNow_.actionLabel + ": " + overlay::StripColorCodes(summaryNow_.what);
    for (const std::string& c : summaryNow_.conditions) line += "  ·  " + c;
    return line;
}

void AddRuleView::Changed() {
    Tidy(draft_, game_);
    offer_ = Offer(draft_, game_);
    if (step_ == Step::Stats) RebuildStats();
    if (Complete(draft_)) {
        const edit::OwnRule own = RuleNow();
        summaryNow_ = Describe(own.rule, loaded_->context);
        checkText_.clear();
        checkText_.push_back("#" + UiText("What it does"));
        switch (draft_.action) {
            case DraftAction::Hide: checkText_.push_back(UiText("Hides them on the ground")); break;
            case DraftAction::Show: checkText_.push_back(UiText("Always shows them: no rule can hide them")); break;
            default:
                checkText_.push_back(UiFormat("A {0} border; never hidden", {UiText(HighlightColors()[static_cast<std::size_t>(draft_.color)].english)}));
                if (draft_.beam) checkText_.push_back(UiText("A light beam where it drops"));
                if (draft_.chat) checkText_.push_back(UiText("A chat line when it drops"));
                break;
        }
        checkText_.push_back("#" + UiFormat("Items ({0})", {std::to_string(summaryNow_.whatAll.size())}));
        for (const std::string& n : summaryNow_.whatAll) checkText_.push_back(n);
        checkText_.push_back("#" + UiText("When"));
        if (summaryNow_.conditions.empty()) checkText_.push_back(UiText("Every time"));
        for (const std::string& c : summaryNow_.conditions) checkText_.push_back(c);
        checkLines_.SetCount(static_cast<int>(checkText_.size()));
    } else {
        checkText_ = {Summary()};
        checkLines_.SetCount(1);
    }
    summary_.text = Summary();
    status_.text.clear();
    ShowStep();  // what a step shows can follow the choices (a highlight's colours)
    SyncControls();
}

void AddRuleView::SyncControls() {
    for (int i = 0; i < kSteps; ++i) {
        overlay::Chip& chip = stepChips_[static_cast<std::size_t>(i)];
        chip.selected = static_cast<int>(step_) == i;
        chip.enabled = StepUsed(static_cast<Step>(i));
    }
    hide_.selected = draft_.action == DraftAction::Hide;
    show_.selected = draft_.action == DraftAction::Show;
    highlight_.selected = draft_.action == DraftAction::Highlight;
    for (std::size_t i = 0; i < colors_.size(); ++i) colors_[i].selected = draft_.color == static_cast<int>(i);
    beam_.on = draft_.beam;
    chat_.on = draft_.chat;

    if (draft_.bases.empty()) {
        chosen_.text = UiText("Nothing chosen yet.");
    } else {
        std::vector<std::string> names;
        for (const std::string& code : draft_.bases) names.push_back(EntryName("itemCodes", Value::String(code), loaded_->context));
        chosen_.text = UiFormat("Chosen ({0}): {1}", {std::to_string(names.size()), JoinNames(names, 4)});
    }
    for (std::size_t i = 0; i < 3; ++i) {
        tiers_[i].enabled = offer_.tierChoices.count(kTiers[i]) != 0;
        tiers_[i].on = tiers_[i].enabled && (draft_.tiers.empty() || draft_.tiers.count(kTiers[i]) != 0);
    }
    for (std::size_t i = 0; i < 3; ++i) myth_[i].selected = draft_.mythical == static_cast<MythicalMode>(i);
    for (std::size_t i = 0; i < rarities_.size(); ++i) {
        const int q = RarityChoices()[i].first;
        rarities_[i].enabled = offer_.rarities.count(q) != 0;
        rarities_[i].on = draft_.rarities.count(q) != 0;
    }
    // The selected stat's "at least".
    const int row = statList_.Selected();
    const StatPick* pick = nullptr;
    const StatOption* option = nullptr;
    if (row >= 0 && row < static_cast<int>(statRows_.size())) {
        option = &offer_.stats[static_cast<std::size_t>(statRows_[static_cast<std::size_t>(row)])];
        for (const StatPick& p : draft_.stats)
            if (p.stat == option->stat && p.param == option->param) pick = &p;
    }
    atLeast_.enabled = pick != nullptr;
    if (option != nullptr) {
        atLeast_.minimum = option->min;
        atLeast_.maximum = option->max;
        atLeast_.step = std::max(1, (option->max - option->min) / 20);
    }
    atLeast_.SetValue(pick != nullptr ? pick->atLeast : (option != nullptr ? option->min : 0));
    for (std::size_t i = 0; i < levels_.size(); ++i)
        levels_[i].on = draft_.levels.empty() || std::find(draft_.levels.begin(), draft_.levels.end(), levelIds_[i]) != draft_.levels.end();
    finish_.enabled = Complete(draft_);
    int last = kSteps - 1;
    next_.visible = static_cast<int>(step_) < last;
}

void AddRuleView::ShowStep() {
    const auto on = [this](Step s, std::initializer_list<Widget*> widgets) {
        for (Widget* w : widgets) w->visible = step_ == s;
    };
    on(Step::Action, {&hide_, &show_, &highlight_});
    const bool colours = step_ == Step::Action && draft_.action == DraftAction::Highlight;
    for (overlay::Chip& c : colors_) c.visible = colours;
    for (Widget* w : std::initializer_list<Widget*>{&colorLabel_, &beam_, &chat_}) w->visible = colours;
    on(Step::Items, {&itemSearch_, &groupList_, &baseList_, &all_, &none_, &chosen_});
    for (overlay::Checkbox& b : tiers_) b.visible = step_ == Step::Tier;
    mythLabel_.visible = step_ == Step::Tier && offer_.mythicals;
    for (overlay::Chip& c : myth_) c.visible = mythLabel_.visible;
    for (overlay::Checkbox& b : rarities_) b.visible = step_ == Step::Rarity;
    const bool stats = step_ == Step::Stats && offer_.statsPossible;
    for (Widget* w : std::initializer_list<Widget*>{&statSearch_, &statList_, &atLeastLabel_, &atLeast_}) w->visible = stats;
    for (overlay::Checkbox& b : levels_) b.visible = step_ == Step::Levels;
    checkLines_.visible = step_ == Step::Check;
    heading_.text = kStepHeadings[static_cast<int>(step_)];
    hint_.text = step_ == Step::Stats && !offer_.statsPossible ? offer_.statsWhy : kStepHints[static_cast<int>(step_)];
}

void AddRuleView::Save(Ui& ui) {
    if (!Complete(draft_) || loaded_->session == nullptr) return;
    const edit::OwnRule own = RuleNow();
    const std::string editing = editing_;
    std::shared_ptr<bool> alive = alive_;
    CommitChange(
        ui, loaded_, alive_,
        [own, editing](edit::Session& s) {
            if (editing.empty()) s.AddOwn(own);
            else s.ReplaceOwn(editing, own);
        },
        [this, alive](Ui& u, bool ok, bool, const std::string& error) {
            if (!*alive) return;
            if (!ok) {
                status_.text = UiFormat("Not saved: {0}", {error});
                return;
            }
            u.PopView();  // back to the rules: the list shows the new rule
        },
        [this](Ui&) { status_.text = UiText("Nothing changed."); });
}

auto AddRuleView::InitialFocus() -> Widget* { return draft_.action == DraftAction::None ? static_cast<Widget*>(&hide_) : &next_; }

auto AddRuleView::Pad(const overlay::PadEvent& event, Ui& ui) -> bool {
    if (event.button == overlay::pad::Button::LB) {
        back_.onClick(ui);
        return true;
    }
    if (event.button == overlay::pad::Button::RB) {
        next_.onClick(ui);
        return true;
    }
    return false;
}

void AddRuleView::Layout(const LayoutContext& c) {
    overlay::Measurer& m = c.measure;
    const Box& area = c.content;
    const overlay::Theme& theme = m.GetTheme();
    const int gap = m.S(8);
    const int chipH = std::max(m.S(28), m.LineHeight(Font::Small) + m.S(12));
    const int rowH = std::max(m.S(28), m.LineHeight(Font::Body) + m.S(8));
    const int buttonH = std::max(m.S(32), m.LineHeight(Font::Body) + m.S(12));

    // The steps along the top.
    int x = area.left;
    for (overlay::Chip& chip : stepChips_) {
        const int w = m.TextWidth(UiText(chip.label), Font::Small) + m.S(20);
        chip.box = Box{x, area.top, x + w, area.top + chipH};
        x += w + m.S(6);
    }
    int y = area.top + chipH + m.S(14);
    heading_.box = Box{area.left, y, area.right, y + m.LineHeight(Font::Bold) + m.S(4)};
    heading_.color = theme.headerGold;
    y = heading_.box.bottom;
    hint_.box = Box{area.left, y, area.right, y + m.LineHeight(Font::Small) * 2 + m.S(4)};
    hint_.color = theme.dim;
    y = hint_.box.bottom + gap;

    // The bottom: the rule in one line, the status, the buttons.
    back_.box = Box{area.left, area.bottom - buttonH, area.left + m.S(120), area.bottom};
    const int finishW = m.TextWidth(UiText(finish_.label), Font::Body) + m.S(40);
    finish_.box = Box{area.right - finishW, area.bottom - buttonH, area.right, area.bottom};
    next_.box = Box{finish_.box.left - gap - m.S(120), area.bottom - buttonH, finish_.box.left - gap, area.bottom};
    status_.box = Box{back_.box.right + gap, area.bottom - buttonH, next_.box.left - gap, area.bottom};
    status_.color = status_.text.rfind(UiText("Not saved"), 0) == 0 ? theme.danger : theme.dim;
    summary_.box = Box{area.left, back_.box.top - gap - m.LineHeight(Font::Small) - m.S(6), area.right, back_.box.top - gap};
    summary_.color = Complete(draft_) ? theme.gold : theme.dim;
    const int bottom = summary_.box.top - gap;
    const Box body{area.left, y, area.right, bottom};

    switch (step_) {
        case Step::Action: {
            int cx = body.left;
            for (overlay::Chip* chip : {&hide_, &show_, &highlight_}) {
                const int w = std::max(m.S(170), m.TextWidth(UiText(chip->label), Font::Body) + m.S(30));
                chip->box = Box{cx, body.top, cx + w, body.top + buttonH + m.S(8)};
                cx += w + gap;
            }
            int cy = body.top + buttonH + m.S(8) + m.S(18);
            colorLabel_.box = Box{body.left, cy, body.right, cy + m.LineHeight(Font::Small) + m.S(4)};
            cy = colorLabel_.box.bottom + m.S(4);
            cx = body.left;
            for (overlay::Chip& chip : colors_) {
                const int w = std::max(m.S(70), m.TextWidth(UiText(chip.label), Font::Small) + m.S(20));
                chip.box = Box{cx, cy, cx + w, cy + chipH};
                cx += w + m.S(6);
            }
            cy += chipH + gap;
            beam_.box = Box{body.left, cy, body.left + m.S(420), cy + rowH};
            cy += rowH;
            chat_.box = Box{body.left, cy, body.left + m.S(420), cy + rowH};
            break;
        }
        case Step::Items: {
            itemSearch_.box = Box{body.left, body.top, body.right, body.top + std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10))};
            const int listTop = itemSearch_.box.bottom + gap;
            const int chosenH = m.LineHeight(Font::Small) * 2 + m.S(6);
            const int listBottom = body.bottom - chosenH - gap;
            const int leftW = std::clamp(body.Width() * 30 / 100, m.S(180), m.S(300));
            groupList_.box = Box{body.left, listTop, body.left + leftW, listBottom};
            groupList_.rowHeight = rowH;
            const int buttonsW = m.S(110);
            baseList_.box = Box{groupList_.box.right + gap, listTop, body.right - buttonsW - gap, listBottom};
            baseList_.rowHeight = rowH;
            all_.box = Box{body.right - buttonsW, listTop, body.right, listTop + buttonH};
            none_.box = Box{body.right - buttonsW, all_.box.bottom + m.S(6), body.right, all_.box.bottom + m.S(6) + buttonH};
            chosen_.box = Box{body.left, listBottom + gap, body.right, body.bottom};
            chosen_.flags = kLeft;
            chosen_.color = theme.text;
            break;
        }
        case Step::Tier: {
            int cy = body.top;
            for (overlay::Checkbox& box : tiers_) {
                box.box = Box{body.left, cy, body.left + m.S(320), cy + rowH};
                cy += rowH;
            }
            cy += m.S(16);
            mythLabel_.box = Box{body.left, cy, body.right, cy + m.LineHeight(Font::Small) + m.S(4)};
            cy = mythLabel_.box.bottom + m.S(4);
            int cx = body.left;
            for (overlay::Chip& chip : myth_) {
                const int w = m.TextWidth(UiText(chip.label), Font::Small) + m.S(24);
                chip.box = Box{cx, cy, cx + w, cy + chipH};
                cx += w + m.S(6);
            }
            break;
        }
        case Step::Rarity: {
            int cy = body.top;
            for (overlay::Checkbox& box : rarities_) {
                box.box = Box{body.left, cy, body.left + m.S(320), cy + rowH};
                cy += rowH;
            }
            break;
        }
        case Step::Stats: {
            statSearch_.box = Box{body.left, body.top, body.right, body.top + std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10))};
            const int numberH = std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10));
            atLeast_.box = Box{body.left + m.S(90), body.bottom - numberH, body.left + m.S(260), body.bottom};
            atLeastLabel_.box = Box{body.left, atLeast_.box.top, atLeast_.box.left - gap, atLeast_.box.bottom};
            statList_.box = Box{body.left, statSearch_.box.bottom + gap, body.right, atLeast_.box.top - gap};
            statList_.rowHeight = rowH;
            break;
        }
        case Step::Levels: {
            int cy = body.top;
            for (overlay::Checkbox& box : levels_) {
                box.box = Box{body.left, cy, body.left + m.S(360), cy + rowH};
                cy += rowH;
            }
            break;
        }
        case Step::Check: {
            checkLines_.box = body;
            checkLines_.rowHeight = std::max(m.S(26), m.LineHeight(Font::Bold) + m.S(8));
            break;
        }
    }
}

void AddRuleView::FocusList(std::vector<Widget*>& out) {
    out.clear();
    for (overlay::Chip& c : stepChips_) out.push_back(&c);
    for (Widget* w : std::initializer_list<Widget*>{&heading_, &hint_, &hide_, &show_, &highlight_, &colorLabel_}) out.push_back(w);
    for (overlay::Chip& c : colors_) out.push_back(&c);
    for (Widget* w : std::initializer_list<Widget*>{&beam_, &chat_, &itemSearch_, &groupList_, &baseList_, &all_, &none_, &chosen_}) out.push_back(w);
    for (overlay::Checkbox& b : tiers_) out.push_back(&b);
    out.push_back(&mythLabel_);
    for (overlay::Chip& c : myth_) out.push_back(&c);
    for (overlay::Checkbox& b : rarities_) out.push_back(&b);
    for (Widget* w : std::initializer_list<Widget*>{&statSearch_, &statList_, &atLeastLabel_, &atLeast_}) out.push_back(w);
    for (overlay::Checkbox& b : levels_) out.push_back(&b);
    for (Widget* w : std::initializer_list<Widget*>{&checkLines_, &summary_, &status_, &back_, &next_, &finish_}) out.push_back(w);
}

void AddRuleView::Paint(Canvas&) {}

void AddRuleView::HelpRows(const Widget*, std::vector<overlay::HelpRow>& out) const {
    out.push_back({overlay::Glyph::LBRB, "", "Previous / next step"});
}

}  // namespace esrfe::ui
