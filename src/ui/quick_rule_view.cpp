#include "quick_rule_view.hpp"

#include <algorithm>
#include <initializer_list>

#include "add_rule_view.hpp"
#include "item_rules_view.hpp"
#include "ui_text.hpp"

namespace esrfe::ui {

using namespace overlay::text_flags;
using overlay::Font;
using overlay::UiFormat;
using overlay::UiText;

namespace {

auto WantLabel(QuickRuleView::Want w) -> const char* {
    switch (w) {
        case QuickRuleView::Want::Hide: return "Hide";
        case QuickRuleView::Want::Style: return "Change the style";
        case QuickRuleView::Want::AlwaysShow: return "Always show";
        case QuickRuleView::Want::Show: return "Stop hiding";
    }
    return "";
}

const char* const kWhereLabels[] = {"This strictness level only", "This level and stricter ones", "Every level"};

auto Join(const std::vector<std::string>& ids) -> std::string {
    std::string out;
    for (std::size_t i = 0; i < ids.size() && i < 3; ++i) out += (i > 0 ? ", " : "") + ids[i];
    if (ids.size() > 3) out += ", …";
    return out;
}

}  // namespace

QuickRuleView::QuickRuleView(std::shared_ptr<Loaded> loaded, ItemRules item, BrowseView::DraftMaker makeDraft)
    : loaded_(std::move(loaded)), item_(std::move(item)), makeDraft_(std::move(makeDraft)) {
    if (makeDraft_ && loaded_->game != nullptr) itemDraft_ = makeDraft_(*loaded_->game);
    if (loaded_->game != nullptr) scopes_ = ScopesFor(itemDraft_, *loaded_->game, loaded_->context);

    // What the filter does with it now.
    heading_.translate = false;
    heading_.font = Font::Title;
    heading_.useThemeColor = false;
    heading_.text = item_.name;
    state_.translate = false;
    state_.font = Font::Body;
    state_.flags = kLeft;
    state_.useThemeColor = false;
    std::string where;
    switch (item_.location) {
        case 0: break;
        case 1: where = UiText("In your inventory, stash or cube."); break;
        case 2: where = UiText("Equipped."); break;
        case 3: where = UiText("At a vendor."); break;
        default: where = UiText("Not on the ground."); break;
    }
    std::string verdict;
    if (item_.hidden) {
        verdict = item_.hiddenUnmatched ? UiText("Hidden: no rule matched it, and the filter hides what no rule matches.")
                  : !item_.hiddenBy.empty() ? UiFormat("Hidden by {0}.", {item_.hiddenBy})
                                            : UiText("Hidden by a rule.");
    } else {
        const std::vector<std::string> showing = ShowingRules();
        verdict = showing.empty() ? UiText("Shown.") : UiFormat("Shown: {0} never hides it.", {Join(showing)});
    }
    if (item_.outsideTown)  // in town every item shows: what matters is what happens out in the field
        state_.text = (where.empty() ? std::string{} : where + "  ") + UiFormat("You are in town, where every item is shown. Outside town: {0}", {verdict});
    else state_.text = where.empty() ? verdict : where + "  " + UiFormat("If it dropped here: {0}", {verdict});
    seeRules_.label = item_.matched.empty() ? std::string("No rule matches it") : UiFormat("The rules that match it ({0})", {std::to_string(item_.matched.size())});
    seeRules_.translate = false;
    seeRules_.enabled = !item_.matched.empty();
    seeRules_.onClick = [this](Ui& ui) { ui.PushView(std::make_shared<ItemRulesView>(loaded_, item_, makeDraft_)); };

    // What the player wants: what fits what it is now first.
    wants_ = item_.hidden ? std::vector<Want>{Want::Show, Want::AlwaysShow, Want::Style} : std::vector<Want>{Want::Hide, Want::Style, Want::AlwaysShow};
    want_ = wants_.front();
    wantLabel_.text = "What do you want to do?";
    wantLabel_.font = Font::Small;
    for (Want w : wants_) {
        overlay::Chip& chip = wantChips_.emplace_back();
        chip.label = WantLabel(w);
        chip.font = Font::Body;
        chip.onClick = [this, w](Ui& ui) {
            Choose(w);
            ui.Repaint();
        };
    }
    // A hidden item: the rule hiding it can be turned off instead.
    const int hider = item_.hiddenBy.empty() ? -1 : loaded_->doc.FindRule(item_.hiddenBy);
    turnOff_.label = UiFormat("Or turn off {0}", {item_.hiddenBy});
    turnOff_.translate = false;
    turnOff_.visible = item_.hidden && hider >= 0 && loaded_->session != nullptr;
    turnOff_.onClick = [this](Ui& ui) {
        const std::string id = item_.hiddenBy;
        CommitChange(
            ui, loaded_, alive_,
            [id](edit::Session& s) {
                if (s.Own(id) != nullptr) s.SetOwnOff(id, true);
                else s.SetOff(id, true);
            },
            [this](Ui& u, bool ok, bool, const std::string& error) {
                if (ok) u.PopView();
                else status_.text = UiFormat("Not saved: {0}", {error});
            },
            nullptr);
    };

    // Which items.
    scopeLabel_.text = "Which items?";
    scopeLabel_.font = Font::Small;
    scopeList_.rowGap = 0;
    scopeList_.SetCount(static_cast<int>(scopes_.size()));
    scopeList_.paintRow = [this](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(scopes_.size())) return;
        const Scope& s = scopes_[static_cast<std::size_t>(index)];
        const overlay::Theme& theme = canvas.GetTheme();
        const bool on = index == scope_;
        const int size = canvas.S(14);
        const Box dot{row.left + canvas.S(8), row.CentreY() - size / 2, row.left + canvas.S(8) + size, row.CentreY() - size / 2 + size};
        canvas.Frame(dot, theme.border);
        if (on) canvas.Fill(dot.Inflated(-canvas.S(3)), theme.gold);
        const std::string count = s.count > 1 ? UiFormat("{0} items", {std::to_string(s.count)}) : std::string{};
        const int countWidth = count.empty() ? 0 : canvas.TextWidth(count, Font::Small) + canvas.S(12);
        const Box text{row.left + canvas.S(30), row.top, row.right - countWidth - canvas.S(6), row.bottom};
        canvas.PushClip(text);
        canvas.Text(s.label, text, on ? theme.text : theme.dim, Font::Body, kVCenter | kSingleLine);
        canvas.PopClip();
        if (!count.empty())
            canvas.Text(count, Box{row.right - countWidth, row.top, row.right - canvas.S(6), row.bottom}, theme.dim, Font::Small, kRight | kVCenter | kSingleLine);
    };
    scopeList_.onClick = [this](int index, Ui& ui) {
        ChooseScope(index);
        ui.Repaint();
    };
    scopeList_.onActivate = scopeList_.onClick;
    scopeList_.onSelect = [this](int index, Ui& ui) {
        ChooseScope(index);
        ui.Repaint();
    };
    if (!scopes_.empty()) scopeList_.Select(0, nullptr);

    // Where.
    whereLabel_.text = "On which strictness levels?";
    whereLabel_.font = Font::Small;
    for (int i = 0; i < 3; ++i) {
        overlay::Chip& chip = whereChips_.emplace_back();
        chip.label = kWhereLabels[i];
        chip.onClick = [this, i](Ui& ui) {
            ChooseWhere(static_cast<Where>(i));
            ui.Repaint();
        };
    }
    note_.translate = false;
    note_.font = Font::Small;
    note_.flags = kLeft;
    note_.useThemeColor = false;
    status_.translate = false;
    status_.font = Font::Small;
    status_.useThemeColor = false;
    back_.label = "< Back";
    back_.onClick = [](Ui& ui) { ui.PopView(); };
    more_.label = "More options…";
    more_.onClick = [this](Ui& ui) { OpenSteps(ui, false); };
    go_.style = overlay::Button::Style::Primary;
    go_.onClick = [this](Ui& ui) {
        if (want_ == Want::Style) OpenSteps(ui, true);
        else Save(ui);
    };
    Choose(want_);
}

auto QuickRuleView::Title() const -> std::string { return UiText("This item"); }

auto QuickRuleView::Wants() const -> std::vector<Want> { return wants_; }

void QuickRuleView::Choose(Want want) {
    want_ = want;
    // Hiding goes on as the player gets stricter; showing and a style are for every level unless asked.
    where_ = want == Want::Hide ? Where::ThisAndStricter : Where::Every;
    Changed();
}

void QuickRuleView::ChooseScope(int index) {
    if (index < 0 || index >= static_cast<int>(scopes_.size())) return;
    scope_ = index;
    Changed();
}

void QuickRuleView::ChooseWhere(Where where) {
    where_ = where;
    Changed();
}

auto QuickRuleView::LevelsFor(Where where) const -> std::vector<std::int64_t> {
    const auto& levels = loaded_->doc.GetSettings().filterLevels;
    const std::int64_t now = loaded_->info.level;
    int at = -1;
    for (std::size_t i = 0; i < levels.size(); ++i)
        if (levels[i].id == now) at = static_cast<int>(i);
    if (where == Where::Every || at < 0) return {};
    if (where == Where::ThisLevel) return levels.size() == 1 ? std::vector<std::int64_t>{} : std::vector<std::int64_t>{now};
    std::vector<std::int64_t> out;
    for (std::size_t i = static_cast<std::size_t>(at); i < levels.size(); ++i) out.push_back(levels[i].id);
    return out.size() == levels.size() ? std::vector<std::int64_t>{} : out;
}

auto QuickRuleView::Draft() const -> RuleDraft {
    RuleDraft d = scope_ < static_cast<int>(scopes_.size()) ? scopes_[static_cast<std::size_t>(scope_)].draft : itemDraft_;
    d.action = want_ == Want::Hide ? DraftAction::Hide : want_ == Want::AlwaysShow ? DraftAction::AlwaysShow : DraftAction::Show;
    d.levels = want_ == Want::AlwaysShow ? std::vector<std::int64_t>{} : LevelsFor(where_);
    if (d.action == DraftAction::Show && d.levels.empty()) d.action = DraftAction::AlwaysShow;  // shown on every level: the same
    if (loaded_->game != nullptr) Tidy(d, *loaded_->game);
    return d;
}

auto QuickRuleView::ShowingRules() const -> std::vector<std::string> {
    std::vector<std::string> out;
    for (const ItemRules::Match& m : item_.matched) {
        const int i = m.id.empty() ? -1 : loaded_->doc.FindRule(m.id);
        if (i >= 0 && loaded_->rules[static_cast<std::size_t>(i)].action == Action::Show) out.push_back(m.id);
    }
    return out;
}

auto QuickRuleView::StyleRules() const -> std::vector<std::string> {
    std::vector<std::string> out;
    for (const ItemRules::Match& m : item_.matched) {
        const int i = m.id.empty() ? -1 : loaded_->doc.FindRule(m.id);
        if (i >= 0 && !loaded_->rules[static_cast<std::size_t>(i)].effects.empty()) out.push_back(m.id);
    }
    return out;
}

void QuickRuleView::Changed() {
    for (std::size_t i = 0; i < wants_.size(); ++i) wantChips_[i].selected = wants_[i] == want_;
    for (int i = 0; i < 3; ++i) whereChips_[static_cast<std::size_t>(i)].selected = static_cast<int>(where_) == i;
    const bool levels = want_ != Want::AlwaysShow && loaded_->doc.GetSettings().filterLevels.size() > 1;
    whereLabel_.visible = levels;
    for (overlay::Chip& c : whereChips_) c.visible = levels;
    switch (want_) {
        case Want::Hide: go_.label = "Hide them"; break;
        case Want::Style: go_.label = "Choose the style…"; break;
        case Want::AlwaysShow: go_.label = "Always show them"; break;
        case Want::Show: go_.label = "Show them"; break;
    }
    go_.enabled = !scopes_.empty() && loaded_->session != nullptr;
    more_.enabled = go_.enabled;
    // What it means next to what the filter does now.
    std::string note;
    if (want_ == Want::Hide) {
        const std::vector<std::string> showing = ShowingRules();
        if (!showing.empty()) note = UiFormat("{0} never hides it now. Your rule comes first, so it hides them anyway.", {Join(showing)});
    } else if (want_ == Want::Show || want_ == Want::AlwaysShow) {
        if (item_.hidden && !item_.hiddenBy.empty())
            note = UiFormat("{0} hides it now. Your rule comes first, so it shows them anyway.", {item_.hiddenBy});
        else if (!item_.hidden && want_ == Want::AlwaysShow)
            note = UiText("It is shown now: always showing keeps it shown when other rules would hide it later.");
    } else {
        const std::vector<std::string> styled = StyleRules();
        note = styled.empty() ? UiText("Shown with your style on the levels you choose.")
                              : UiFormat("Your style comes after {0}, so its colours and sound win.", {Join(styled)});
    }
    note_.text = note;
}

void QuickRuleView::Save(Ui& ui) {
    if (loaded_->game == nullptr || loaded_->session == nullptr) return;
    const RuleDraft d = Draft();
    if (!Complete(d)) return;
    // The title: what it is about and when, as the list says it.
    const std::vector<edit::OwnRule> draftRules = ToRules(d, *loaded_->game, "x");
    if (draftRules.empty()) return;
    const RuleSummary s = Describe(draftRules.front().rule, loaded_->context);
    std::string title = scope_ < static_cast<int>(scopes_.size()) ? scopes_[static_cast<std::size_t>(scope_)].label : overlay::StripColorCodes(s.what);
    for (const std::string& c : s.conditions) title += ", " + c;
    if (title.size() > 120) title = title.substr(0, 117) + "...";
    const std::vector<edit::OwnRule> rules = ToRules(d, *loaded_->game, title);
    CommitChange(
        ui, loaded_, alive_,
        [rules](edit::Session& session) {
            for (const edit::OwnRule& r : rules) session.AddOwn(r);
        },
        [this](Ui& u, bool ok, bool, const std::string& error) {
            if (ok) u.PopView();  // back to the rules: the list shows the new rule
            else status_.text = UiFormat("Not saved: {0}", {error});
        },
        nullptr);
}

void QuickRuleView::OpenSteps(Ui& ui, bool style) {
    auto steps = std::make_shared<AddRuleView>(loaded_, Draft(), std::string{});
    steps->GoTo(style ? AddRuleView::Step::Look : AddRuleView::Step::Items);
    ui.PushView(steps);
}

auto QuickRuleView::InitialFocus() -> Widget* { return wantChips_.empty() ? static_cast<Widget*>(&back_) : &wantChips_.front(); }

void QuickRuleView::Layout(const LayoutContext& c) {
    overlay::Measurer& m = c.measure;
    const Box& area = c.content;
    const overlay::Theme& theme = m.GetTheme();
    const int gap = m.S(8);
    const int chipH = std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10));
    const int buttonH = std::max(m.S(32), m.LineHeight(Font::Body) + m.S(12));
    const int small = m.LineHeight(Font::Small) + m.S(4);
    int y = area.top;
    heading_.box = Box{area.left, y, area.right, y + m.LineHeight(Font::Title) + m.S(6)};
    heading_.color = theme.headerGold;
    y = heading_.box.bottom;
    const int rulesW = m.TextWidth(seeRules_.label, Font::Body) + m.S(30);
    state_.box = Box{area.left, y, area.right - rulesW - gap, y + buttonH};
    state_.color = item_.hidden ? theme.danger : theme.text;
    seeRules_.box = Box{area.right - rulesW, y, area.right, y + buttonH};
    y += buttonH + gap * 2;

    wantLabel_.box = Box{area.left, y, area.right, y + small};
    y += small;
    int x = area.left;
    for (overlay::Chip& chip : wantChips_) {
        const int w = std::max(m.S(140), m.TextWidth(UiText(chip.label), Font::Body) + m.S(30));
        chip.box = Box{x, y, x + w, y + chipH};
        x += w + gap;
    }
    if (turnOff_.visible) {
        const int w = m.TextWidth(turnOff_.label, Font::Body) + m.S(30);
        turnOff_.box = Box{x + gap * 2, y, x + gap * 2 + w, y + chipH};
    }
    y += chipH + gap * 2;

    back_.box = Box{area.left, area.bottom - buttonH, area.left + m.S(120), area.bottom};
    const int goW = std::max(m.S(160), m.TextWidth(UiText(go_.label), Font::Body) + m.S(40));
    go_.box = Box{area.right - goW, area.bottom - buttonH, area.right, area.bottom};
    const int moreW = m.TextWidth(UiText(more_.label), Font::Body) + m.S(30);
    more_.box = Box{go_.box.left - gap - moreW, area.bottom - buttonH, go_.box.left - gap, area.bottom};
    status_.box = Box{back_.box.right + gap, area.bottom - buttonH, more_.box.left - gap, area.bottom};
    status_.color = theme.danger;
    note_.box = Box{area.left, back_.box.top - gap - small * 2, area.right, back_.box.top - gap};
    note_.color = theme.gold;
    int bottom = note_.box.top - gap;
    if (whereLabel_.visible) {
        const int top = bottom - small - chipH;
        whereLabel_.box = Box{area.left, top, area.right, top + small};
        int wx = area.left;
        for (overlay::Chip& chip : whereChips_) {
            const int w = m.TextWidth(UiText(chip.label), Font::Small) + m.S(28);
            chip.box = Box{wx, top + small, wx + w, top + small + chipH};
            wx += w + gap;
        }
        bottom = top - gap;
    }
    scopeLabel_.box = Box{area.left, y, area.right, y + small};
    y += small;
    scopeList_.box = Box{area.left, y, std::min(area.right, area.left + m.S(900)), bottom};
    scopeList_.rowHeight = std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10));
}

void QuickRuleView::FocusList(std::vector<Widget*>& out) {
    out = {&heading_, &state_, &seeRules_, &wantLabel_};
    for (overlay::Chip& c : wantChips_) out.push_back(&c);
    out.push_back(&turnOff_);
    out.push_back(&scopeLabel_);
    out.push_back(&scopeList_);
    out.push_back(&whereLabel_);
    for (overlay::Chip& c : whereChips_) out.push_back(&c);
    for (Widget* w : std::initializer_list<Widget*>{&note_, &status_, &back_, &more_, &go_}) out.push_back(w);
}

}  // namespace esrfe::ui
