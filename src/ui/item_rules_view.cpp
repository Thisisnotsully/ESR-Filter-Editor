#include "item_rules_view.hpp"

#include <algorithm>

#include "add_rule_view.hpp"
#include "rule_draft.h"
#include "rule_view.hpp"
#include "ui_text.hpp"

namespace esrfe::ui {

using namespace overlay::text_flags;
using overlay::Font;
using overlay::UiFormat;
using overlay::UiText;

namespace {

// What a rule does, in a few words: its action, then its effects ("Sends a chat line", "Plays a sound").
auto Does(const RuleSummary& s) -> std::string {
    std::string text = s.actionLabel;
    std::size_t shown = 0;
    for (const std::string& e : s.effects) {
        if (shown == 3) {
            text += ", …";
            break;
        }
        text += (shown == 0 ? ": " : ", ") + overlay::StripColorCodes(e);
        ++shown;
    }
    return text;
}

}  // namespace

ItemRulesView::ItemRulesView(std::shared_ptr<Loaded> loaded, ItemRules item, DraftMaker makeDraft)
    : loaded_(std::move(loaded)), item_(std::move(item)), makeDraft_(std::move(makeDraft)) {
    for (const ItemRules::Match& m : item_.matched) {
        Row row;
        row.id = m.id;
        row.hides = !m.id.empty() && m.id == item_.hiddenBy;
        row.rule = m.id.empty() ? -1 : loaded_->doc.FindRule(m.id);
        if (row.rule >= 0) {
            const RuleSummary& s = loaded_->rules[static_cast<std::size_t>(row.rule)];
            row.head = m.id + "  ·  " + Does(s);
            row.line = overlay::StripColorCodes(s.what);
            for (const std::string& c : s.conditions) row.line += "  ·  " + c;
        } else {
            row.head = m.id.empty() ? UiFormat("Rule #{0} of the file", {std::to_string(m.number)}) : m.id;
            row.line = m.title.empty() ? UiText("Not in the editor's list: its title has no id.") : overlay::StripColorCodes(m.title);
        }
        if (row.hides) row.head += "  ·  " + UiText("hides it");
        rows_.push_back(std::move(row));
    }

    heading_.translate = false;
    heading_.font = Font::Title;
    heading_.useThemeColor = false;
    heading_.text = item_.name;
    state_.translate = false;
    state_.font = Font::Body;
    state_.useThemeColor = false;
    if (item_.hidden) {
        if (item_.hiddenUnmatched) state_.text = UiText("Hidden: no rule matched it, and the filter hides what no rule matches.");
        else if (!item_.hiddenBy.empty()) state_.text = UiFormat("Hidden by {0}.", {item_.hiddenBy});
        else state_.text = UiText("Hidden by a rule.");
    } else {
        state_.text = UiText("Shown on the ground.");
    }
    if (rows_.empty()) state_.text += "  " + UiText("No rule matched it.");
    else state_.text += "  " + UiFormat("Rules that match it: {0}.", {std::to_string(rows_.size())});
    if (item_.more) state_.text += "  " + UiText("(Only the first ones are listed.)");
    hint_.text = "Open a rule to turn it off or change it, or add a rule of your own for this item.";
    hint_.font = Font::Small;
    hint_.flags = kLeft;
    hint_.useThemeColor = false;

    list_.rowGap = 0;
    list_.emptyText = "No rule matched it";
    list_.SetCount(static_cast<int>(rows_.size()));
    list_.paintRow = [this](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(rows_.size())) return;
        const Row& r = rows_[static_cast<std::size_t>(index)];
        const overlay::Theme& theme = canvas.GetTheme();
        const int half = row.Height() / 2;
        const Box head{row.left + canvas.S(10), row.top + canvas.S(2), row.right - canvas.S(90), row.top + half};
        const Box line{row.left + canvas.S(10), row.top + half, row.right - canvas.S(90), row.bottom - canvas.S(2)};
        canvas.PushClip(Box{row.left, row.top, row.right - canvas.S(90), row.bottom});
        canvas.Text(r.head, head, r.hides ? theme.danger : theme.headerGold, Font::Bold, kVCenter | kSingleLine);
        canvas.Text(r.line, line, r.rule >= 0 ? theme.text : theme.dim, Font::Small, kVCenter | kSingleLine);
        canvas.PopClip();
        if (r.rule >= 0)
            canvas.Text(UiText("Open >"), Box{row.right - canvas.S(86), row.top, row.right - canvas.S(10), row.bottom}, theme.gold, Font::Small,
                        kRight | kVCenter | kSingleLine);
    };
    list_.onClick = [this](int index, Ui& ui) { OpenRow(index, ui); };
    list_.onActivate = [this](int index, Ui& ui) { OpenRow(index, ui); };

    add_.label = "Add a new rule for this item";
    add_.style = overlay::Button::Style::Primary;
    add_.enabled = loaded_->session != nullptr && loaded_->game != nullptr;
    add_.onClick = [this](Ui& ui) {
        RuleDraft draft = makeDraft_ ? makeDraft_(*loaded_->game) : RuleDraft{};
        ui.PushView(std::make_shared<AddRuleView>(loaded_, std::move(draft), std::string{}));
    };
    back_.label = "< Back";
    back_.onClick = [](Ui& ui) { ui.PopView(); };
}

auto ItemRulesView::Title() const -> std::string { return UiText("This item in the filter"); }

void ItemRulesView::OpenRow(int index, Ui& ui) {
    if (index < 0 || index >= static_cast<int>(rows_.size())) return;
    const Row& r = rows_[static_cast<std::size_t>(index)];
    if (r.rule < 0) return;
    ui.PushView(std::make_shared<RuleView>(loaded_, r.rule));
}

auto ItemRulesView::InitialFocus() -> Widget* { return rows_.empty() ? static_cast<Widget*>(&add_) : &list_; }

void ItemRulesView::Layout(const LayoutContext& c) {
    overlay::Measurer& m = c.measure;
    const Box& area = c.content;
    const overlay::Theme& theme = m.GetTheme();
    const int gap = m.S(8);
    const int buttonH = std::max(m.S(32), m.LineHeight(Font::Body) + m.S(12));
    int y = area.top;
    heading_.box = Box{area.left, y, area.right, y + m.LineHeight(Font::Title) + m.S(6)};
    heading_.color = theme.headerGold;
    y = heading_.box.bottom;
    state_.box = Box{area.left, y, area.right, y + m.LineHeight(Font::Body) + m.S(6)};
    state_.color = item_.hidden ? theme.danger : theme.text;
    y = state_.box.bottom;
    hint_.box = Box{area.left, y, area.right, y + m.LineHeight(Font::Small) + m.S(6)};
    hint_.color = theme.dim;
    y = hint_.box.bottom + gap;
    back_.box = Box{area.left, area.bottom - buttonH, area.left + m.S(120), area.bottom};
    const int addW = m.TextWidth(UiText(add_.label), Font::Body) + m.S(40);
    add_.box = Box{area.right - addW, area.bottom - buttonH, area.right, area.bottom};
    list_.box = Box{area.left, y, area.right, add_.box.top - gap};
    list_.rowHeight = m.LineHeight(Font::Bold) + m.LineHeight(Font::Small) + m.S(14);
    list_.rowGap = m.S(4);
}

void ItemRulesView::FocusList(std::vector<Widget*>& out) { out = {&heading_, &state_, &hint_, &list_, &back_, &add_}; }

}  // namespace esrfe::ui
