#include "help_view.hpp"

#include <algorithm>

#include "ui_text.hpp"

namespace esrfe::ui {

using namespace overlay::text_flags;
using overlay::Font;
using overlay::UiText;

auto HelpTopics() -> const std::vector<HelpTopic>& {
    static const std::vector<HelpTopic> topics = {
        {"Getting around",
         {
             "The editor shows the loot filter the game uses now, part by part. Choose a part on the left, then a rule on the right.",
             "Search finds rules by what they do, the items they name, or their id (R121, U001).",
             "Filter, in the top bar, switches the game to another filter: one of yours, the author's, the mod's, or one you made.",
             "Filter volume sets how loud the filter's sounds are. Test plays one at that volume.",
             "#With a controller",
             "LB and RB move between parts and steps. A chooses, B goes back. A text box opens the on-screen keyboard.",
         }},
        {"The edit-item key",
         {
             "Bind \"Edit Hovered Item (Filter Editor)\" to a key. Then point at an item and press it: on the ground, in your inventory, stash or cube, equipped, or at a vendor.",
             "It shows what the filter does with the item now, and which rules match it.",
             "Then it asks what you want: hide it, change its style, always show it. For an item that is hidden: stop hiding it, or turn off the rule that hides it.",
             "Which items: only this one, or everything like it, step by step out to every item of its kind (all superior elite weapons, every magic item, Ber and every higher rune, ...).",
             "On which strictness levels: this one, this one and stricter ones, or every level.",
             "One button makes the rule. More options opens every step of a new rule, filled in.",
             "An item in your inventory is judged as if it had just dropped where you stand.",
         }},
        {"Changing a rule",
         {
             "Open a rule to see what it does, on which strictness levels, and for which items.",
             "Rule is on: switch a rule off without deleting it.",
             "Change what it does (Hide, Always show or neither), its strictness levels, or take items out of it.",
             "Undo puts the author's version of the rule back.",
             "Your own rules (U001, U002, ...) can be changed again or deleted.",
         }},
        {"Adding a rule",
         {
             "+ Add a rule goes through the steps: what happens, item type, items, tier, stats, strictness, style, check.",
             "Hide hides the items on the strictness levels you pick. Show keeps them shown there. Always Show keeps them shown on every level.",
             "Your rules come first in the filter: what they hide is hidden, what they show is never hidden.",
             "Style: border, background and name colours, a light beam, a drop sound, a chat line and a minimap icon. The pictures show how it will look.",
             "Rules with stats only work on identified items.",
         }},
        {"Your own versions",
         {
             "The author's filter is never changed. Your first change makes your own version of it: you name it, and the game switches to it.",
             "You can have several versions of one filter, each with its own changes. Choose one in the Filter list.",
             "When the author updates the filter, your changes are put on top of the new version. If you both changed the same thing, the editor asks which to keep.",
         }},
        {"A new filter",
         {
             "+ New filter..., at the bottom of the Filter list, makes a filter from scratch. It starts from Sully's QoL Filter's choices.",
             "Every step is already filled in: change what you like, or go straight to Check.",
             "Each part can be turned off, and each of its steps given other strictness levels, a character level, a gold amount, or added again.",
             "Style sets the colours of the label styles. Create makes the filter and switches the game to it.",
         }},
        {"Good to know",
         {
             "Strictness levels: a filter has a few, from showing the most to hiding the most. Bind the loot filter's \"Next Filter Level\" key to switch between them in game.",
             "Bind \"Show Hidden Items\" too: while it is on, hidden items show on the ground, and you can point at them with the edit-item key.",
             "Changes are saved at once, and the game loads the filter again by itself.",
             "Your versions are in your loot filter folder (Saved Games, Diablo II Resurrected, mods, Eastern_Sun_Resurrected, LootFilters); your changes are kept in its esr-filter-editor folder.",
         }},
    };
    return topics;
}

HelpView::HelpView() {
    topics_.rowGap = 0;
    topics_.SetCount(static_cast<int>(HelpTopics().size()));
    topics_.paintRow = [](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(HelpTopics().size())) return;
        canvas.Text(UiText(HelpTopics()[static_cast<std::size_t>(index)].title), Box{row.left + canvas.S(10), row.top, row.right - canvas.S(6), row.bottom},
                    canvas.GetTheme().text, Font::Body, kVCenter | kSingleLine);
    };
    topics_.onSelect = [this](int index, Ui& ui) {
        ChooseTopic(index);
        ui.Repaint();
    };
    topics_.onClick = [this](int index, Ui& ui) {
        topics_.Select(index, nullptr);
        ChooseTopic(index);
        ui.Repaint();
    };
    page_.rowGap = 0;
    page_.drawRowBackground = false;
    page_.paintRow = [this](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
        const HelpTopic& topic = HelpTopics()[static_cast<std::size_t>(topic_)];
        if (index < 0 || index >= static_cast<int>(topic.paragraphs.size())) return;
        const std::string text = topic.paragraphs[static_cast<std::size_t>(index)];
        const overlay::Theme& theme = canvas.GetTheme();
        const bool heading = !text.empty() && text[0] == '#';
        const Box box{row.left + canvas.S(10), row.top + canvas.S(4), row.right - canvas.S(10), row.bottom - canvas.S(2)};
        canvas.PushClip(box);
        if (heading) canvas.Text(UiText(text.substr(1)), box, theme.headerGold, Font::Bold, kLeft | kVCenter | kSingleLine);
        else canvas.Text(UiText(text), box, theme.text, Font::Body, kLeft);
        canvas.PopClip();
    };
    back_.label = "< Back";
    back_.onClick = [](Ui& ui) { ui.PopView(); };
    topics_.Select(0, nullptr);
    ChooseTopic(0);
}

auto HelpView::Title() const -> std::string { return UiText("Help"); }

void HelpView::ChooseTopic(int index) {
    if (index < 0 || index >= static_cast<int>(HelpTopics().size())) return;
    topic_ = index;
    page_.SetCount(static_cast<int>(HelpTopics()[static_cast<std::size_t>(index)].paragraphs.size()));
    page_.SetScroll(0);
}

void HelpView::Layout(const LayoutContext& c) {
    overlay::Measurer& m = c.measure;
    const Box& area = c.content;
    const int gap = m.S(10);
    const int buttonH = std::max(m.S(32), m.LineHeight(Font::Body) + m.S(12));
    back_.box = Box{area.left, area.bottom - buttonH, area.left + m.S(120), area.bottom};
    const int leftW = std::clamp(area.Width() * 24 / 100, m.S(180), m.S(300));
    topics_.box = Box{area.left, area.top, area.left + leftW, back_.box.top - gap};
    topics_.rowHeight = std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10));
    page_.box = Box{topics_.box.right + gap * 2, area.top, std::min(area.right, topics_.box.right + gap * 2 + m.S(1000)), back_.box.top - gap};
    // Rows as tall as this topic's longest paragraph at this width.
    int tallest = m.LineHeight(Font::Body);
    for (const char* p : HelpTopics()[static_cast<std::size_t>(topic_)].paragraphs)
        if (p[0] != '#') tallest = std::max(tallest, m.TextHeight(UiText(p), page_.box.Width() - m.S(20), Font::Body));
    page_.rowHeight = tallest + m.S(14);
}

void HelpView::FocusList(std::vector<Widget*>& out) { out = {&topics_, &page_, &back_}; }

}  // namespace esrfe::ui
