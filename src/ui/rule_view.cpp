#include "rule_view.hpp"

#include <algorithm>

#include "ui_text.hpp"

namespace esrfe::ui {

using namespace overlay::text_flags;
using overlay::Font;
using overlay::UiFormat;
using overlay::UiText;

RuleView::RuleView(std::shared_ptr<const Loaded> loaded, int rule) : loaded_(std::move(loaded)), rule_(rule) {
    const RuleSummary& s = loaded_->rules[static_cast<std::size_t>(rule_)];
    const document::Rule& r = loaded_->doc.Rules()[static_cast<std::size_t>(rule_)];
    const auto add = [this](Line::Kind kind, std::string text) { content_.push_back(Line{kind, std::move(text)}); };

    add(Line::Kind::Heading, s.whatAll.size() > 1 ? UiFormat("Items ({0})", {std::to_string(s.whatAll.size())}) : UiText("Items"));
    if (s.whatAll.empty()) add(Line::Kind::Item, UiText("Any item"));
    for (const std::string& name : s.whatAll) add(Line::Kind::Item, name);
    add(Line::Kind::Heading, UiText("When"));
    if (s.conditions.empty()) add(Line::Kind::Item, UiText("Every time"));
    for (const std::string& c : s.conditions) add(Line::Kind::Item, c);
    add(Line::Kind::Heading, UiText("What it does"));
    switch (s.action) {
        case Action::Hide: add(Line::Kind::Item, UiText("Hides the item on the ground")); break;
        case Action::Show: add(Line::Kind::Item, UiText("Always shows the item: later rules cannot hide it")); break;
        default: break;
    }
    if (!s.preview.empty()) add(Line::Kind::Preview, s.preview);
    for (const std::string& e : s.effects) {
        if (!s.preview.empty() && e == UiFormat("Shown as: {0}", {s.preview})) continue;
        add(Line::Kind::Item, e);
    }
    if (s.action == Action::Other && s.effects.empty()) add(Line::Kind::Item, UiText("Nothing the editor knows how to describe"));
    add(Line::Kind::Heading, UiText("About this rule"));
    if (!r.title.empty()) add(Line::Kind::Note, r.title);
    const std::string place = loaded_->Place(rule_);
    if (!place.empty()) add(Line::Kind::Note, UiFormat("In: {0}", {place}));
    add(Line::Kind::Note, UiFormat("Rule {0} of {1}; rules run from the top, and the first one that hides an item wins.",
                                   {std::to_string(rule_ + 1), std::to_string(loaded_->doc.ActiveCount())}));

    badge_.translate = false;
    badge_.font = Font::Bold;
    badge_.flags = kCenter | kVCenter | kSingleLine;
    badge_.useThemeColor = false;
    badge_.text = s.actionLabel;
    what_.translate = false;
    what_.font = Font::Title;
    what_.flags = kLeft | kVCenter | kSingleLine;
    what_.text = s.what;

    lines_.drawRowBackground = false;
    lines_.rowGap = 0;
    lines_.paintRow = [this](overlay::Canvas& canvas, int index, const overlay::Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(content_.size())) return;
        const Line& line = content_[static_cast<std::size_t>(index)];
        const overlay::Theme& theme = canvas.GetTheme();
        const overlay::Box text{row.left + canvas.S(line.kind == Line::Kind::Heading ? 8 : 24), row.top, row.right - canvas.S(8), row.bottom};
        canvas.PushClip(text);
        switch (line.kind) {
            case Line::Kind::Heading: canvas.Text(line.text, text, theme.headerGold, Font::Bold, kVCenter | kSingleLine); break;
            case Line::Kind::Item: canvas.Text(line.text, text, theme.text, Font::Body, kVCenter | kSingleLine); break;
            case Line::Kind::Preview: {
                const std::string label = UiText("Shown as:") + " ";
                const int w = canvas.TextWidth(label, Font::Body);
                canvas.Text(label, text, theme.text, Font::Body, kVCenter | kSingleLine);
                // The new name in the game's colours on a dark strip, as it looks on the ground.
                overlay::Box name{text.left + w, row.top + canvas.S(2), std::min(text.right, text.left + w + canvas.TextWidth(line.text, Font::Bold) + canvas.S(16)), row.bottom - canvas.S(2)};
                canvas.FillAlpha(name, overlay::Rgb{0, 0, 0}, 200);
                canvas.Text(line.text, name, theme.body, Font::Bold, kCenter | kVCenter | kSingleLine);
                break;
            }
            case Line::Kind::Note: canvas.Text(line.text, text, theme.dim, Font::Small, kVCenter | kSingleLine); break;
        }
        canvas.PopClip();
    };
    lines_.rowTooltip = [this](int index) -> std::string {
        if (index < 0 || index >= static_cast<int>(content_.size())) return {};
        return overlay::StripColorCodes(content_[static_cast<std::size_t>(index)].text);
    };

    back_.label = "< Back";
    back_.onClick = [](Ui& ui) { ui.PopView(); };
}

auto RuleView::Title() const -> std::string {
    const document::Rule& r = loaded_->doc.Rules()[static_cast<std::size_t>(rule_)];
    return r.id.empty() ? UiFormat("Rule {0}", {std::to_string(rule_ + 1)}) : UiFormat("Rule {0}", {r.id});
}

void RuleView::Layout(const LayoutContext& c) {
    overlay::Measurer& m = c.measure;
    const overlay::Box& area = c.content;
    const RuleSummary& s = loaded_->rules[static_cast<std::size_t>(rule_)];
    const int head = std::max(m.S(36), m.LineHeight(Font::Title) + m.S(12));
    badge_.box = overlay::Box{area.left, area.top, area.left + m.S(130), area.top + head};
    badge_.color = ActionColor(s.action, m.GetTheme());
    what_.box = overlay::Box{badge_.box.right + m.S(12), area.top, area.right, area.top + head};
    const int buttonHeight = std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10));
    back_.box = overlay::Box{area.left, area.bottom - buttonHeight, area.left + m.S(120), area.bottom};
    lines_.box = overlay::Box{area.left, badge_.box.bottom + m.S(8), area.right, back_.box.top - m.S(8)};
    lines_.rowHeight = std::max(m.S(26), m.LineHeight(Font::Bold) + m.S(8));
    lines_.SetCount(static_cast<int>(content_.size()));
}

void RuleView::Paint(Canvas& canvas) {
    const overlay::Rgb color = badge_.color;
    canvas.FillAlpha(badge_.box, color, 64);
    canvas.Frame(badge_.box, color);
}

void RuleView::FocusList(std::vector<Widget*>& out) { out = {&badge_, &what_, &lines_, &back_}; }

void RuleView::HelpRows(const Widget*, std::vector<overlay::HelpRow>& out) const {
    out.push_back({overlay::Glyph::B, "Esc", "Back to the rules"});
}

}  // namespace esrfe::ui
