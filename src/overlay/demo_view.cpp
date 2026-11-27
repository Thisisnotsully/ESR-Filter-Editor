#include "demo_view.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "text_fold.hpp"
#include "ui_text.hpp"

namespace esrfe::overlay {
namespace {

using namespace text_flags;

constexpr const char* kBases[] = {
    "Harlequin Crest", "Shako", "Colossus Blade", "Ber Rune", "Jah Rune", "Small Charm", "Grand Charm", "Archon Plate",
    "Monarch", "Thunder Maul", "Hydra Bow", "Diadem", "Stag Bow", "Perfect Amethyst", "Ring", "Amulet", "Jewel",
    "Sacred Armor", "Phase Blade", "Berserker Axe", "Mythical Ember of Creation", "Crystal Sword", "Ume Rune",
    "Ethereal Giant Thresher", "Gul Rune", "Skull Cap", "Vortex Shield", "Ancient's Pledge", "Chipped Topaz",
};
// ÿc colour codes: unique gold, set green, rare yellow, magic blue, crafted orange, gray, white, purple.
constexpr char kCodes[] = {'4', '2', '9', '3', '8', '5', '0', ';'};

class DetailView final : public View {
public:
    explicit DetailView(std::string row) : row_(std::move(row)) {
        text_.text = row_;
        text_.translate = false;
        text_.flags = kLeft;
        text_.font = Font::Bold;
        back_.label = "< Back";
        back_.onClick = [](Ui& ui) { ui.PopView(); };
    }
    [[nodiscard]] auto Title() const -> std::string override { return UiText("Row details"); }
    void Layout(const LayoutContext& c) override {
        Measurer& m = c.measure;
        panel_.box = c.content;
        panel_.title = "A pushed view";
        text_.box = Box{c.content.left + m.S(16), c.content.top + m.S(40), c.content.right - m.S(16), c.content.top + m.S(120)};
        const int h = std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10));
        back_.box = Box{c.content.left + m.S(16), c.content.bottom - m.S(16) - h, c.content.left + m.S(16) + m.S(120), c.content.bottom - m.S(16)};
    }
    void FocusList(std::vector<Widget*>& out) override { out = {&panel_, &text_, &back_}; }
    auto InitialFocus() -> Widget* override { return &back_; }
    void HelpRows(const Widget*, std::vector<HelpRow>& out) const override { out.push_back({Glyph::B, "", "Back to the list"}); }

private:
    std::string row_;
    Panel panel_;
    Label text_;
    Button back_;
};

}  // namespace

DemoView::DemoView(int rows) {
    rows_.reserve(static_cast<std::size_t>(rows));
    for (int i = 0; i < rows; ++i) {
        const char code = kCodes[static_cast<std::size_t>(i) % std::size(kCodes)];
        const char* base = kBases[static_cast<std::size_t>(i * 7 + i / 13) % std::size(kBases)];
        char id[16];
        std::snprintf(id, sizeof id, "R%04d", i + 1);
        std::string row = std::string(kColorMarker) + "7" + id + std::string(kColorMarker) + code + " " + base;
        if (i % 5 == 0) row += std::string(kColorMarker) + "5 (ethereal)";
        rows_.push_back(std::move(row));
        searchable_.push_back(text::SearchTextUtf8(StripColorCodes(rows_.back())));
    }

    search_.search = true;
    search_.placeholder = "Search the rows…";
    search_.onChange = [this](const std::string& text, Ui& ui) {
        highlight_ = MakeHighlight(text);
        Refilter();
        list_.SetCount(static_cast<int>(shown_.size()));
        list_.Select(0, nullptr);
        list_.SetScroll(0);
        ui.Repaint();
    };
    search_.onSubmit = [this](const std::string&, Ui& ui) {
        if (!shown_.empty()) ui.Focus(&list_);
    };
    search_.tooltip = "Words must all be found; \"quoted words\" together; -word leaves rows out.";

    count_.font = Font::Small;
    count_.translate = false;
    count_.useThemeColor = false;

    list_.rowHeight = 26;
    list_.paintRow = [this](Canvas& canvas, int index, const Box& row, const ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(shown_.size())) return;
        canvas.SetHighlight(&highlight_);
        canvas.Text(rows_[static_cast<std::size_t>(shown_[static_cast<std::size_t>(index)])],
                    Box{row.left + canvas.S(12), row.top, row.right - canvas.S(12), row.bottom}, canvas.GetTheme().headerGold, Font::Bold,
                    kVCenter | kSingleLine);
        canvas.SetHighlight(nullptr);
    };
    list_.onSelect = [this](int index, Ui& ui) {
        if (index >= 0 && index < static_cast<int>(shown_.size())) SetStatus(UiFormat("Row {0} selected", {std::to_string(shown_[static_cast<std::size_t>(index)] + 1)}), ui);
    };
    list_.onActivate = [this](int index, Ui& ui) {
        if (index < 0 || index >= static_cast<int>(shown_.size())) return;
        ui.PushView(std::make_shared<DetailView>(rows_[static_cast<std::size_t>(shown_[static_cast<std::size_t>(index)])]));
    };
    list_.rowTooltip = [this](int index) -> std::string {
        if (index < 0 || index >= static_cast<int>(shown_.size())) return {};
        const int row = shown_[static_cast<std::size_t>(index)];
        return StripColorCodes(rows_[static_cast<std::size_t>(row)]) + "\n" + UiFormat("Row {0} of {1}", {std::to_string(row + 1), std::to_string(rows_.size())});
    };

    controls_.title = "Controls";

    ethereal_.label = "Show ethereal items";
    ethereal_.on = true;
    ethereal_.onChange = [this](bool on, Ui& ui) { SetStatus(on ? UiText("Ethereal items shown") : UiText("Ethereal items hidden"), ui); };
    ethereal_.tooltip = "A toggle: A, Space or a click switches it.";

    sound_.label = "Play a sound";
    sound_.onChange = [this](bool on, Ui& ui) { SetStatus(on ? UiText("Sound on") : UiText("Sound off"), ui); };

    qualityLabel_.text = "Quality";
    quality_.options = {"Any", "Normal", "Superior", "Magic", "Rare", "Set", "Unique", "Crafted", "Low quality",
                        "Ethereal", "Runeword", "Mythical"};
    quality_.selected = 0;
    quality_.onChange = [this](int index, Ui& ui) { SetStatus(UiFormat("Quality: {0}", {quality_.SelectedText()}), ui); (void)index; };

    levelLabel_.text = "Item level";
    level_.minimum = 0;
    level_.maximum = 99;
    level_.value = 85;
    level_.onChange = [this](int value, Ui& ui) { SetStatus(UiFormat("Item level {0}", {std::to_string(value)}), ui); };

    noteLabel_.text = "Note";
    note_.placeholder = "Type a note";
    note_.keyboardExitDown = true;
    note_.onSubmit = [this](const std::string& text, Ui& ui) { SetStatus(UiFormat("Note: {0}", {text}), ui); };

    sortLabel_.text = "Sort:";
    sortLabel_.font = Font::Small;
    sortNumber_.label = "Number";
    sortName_.label = "A-Z";
    sortNameDown_.label = "Z-A";
    sortNumber_.selected = true;
    sortNumber_.onClick = [this](Ui& ui) { SetSort(Sort::Number, ui); };
    sortName_.onClick = [this](Ui& ui) { SetSort(Sort::Name, ui); };
    sortNameDown_.onClick = [this](Ui& ui) { SetSort(Sort::NameDown, ui); };

    open_.label = "Open row";
    open_.style = Button::Style::Primary;
    open_.onClick = [this](Ui& ui) { list_.onActivate(list_.Selected(), ui); };
    remove_.label = "Delete row…";
    remove_.style = Button::Style::Danger;
    remove_.onClick = [this](Ui& ui) {
        const int index = list_.Selected();
        if (index < 0 || index >= static_cast<int>(shown_.size())) {
            SetStatus(UiText("Select a row first"), ui);
            return;
        }
        const int row = shown_[static_cast<std::size_t>(index)];
        DialogSpec spec;
        spec.title = "Delete this row?";
        spec.message = rows_[static_cast<std::size_t>(row)] + "\n\n" + UiText("It is only a demo: the row goes until the game restarts.");
        spec.confirm = "Delete";
        spec.danger = true;
        ui.ShowDialog(spec, [this, row](bool confirmed) {
            if (!confirmed) {
                status_ = UiText("Nothing deleted");
                return;
            }
            rows_.erase(rows_.begin() + row);
            searchable_.erase(searchable_.begin() + row);
            Refilter();
            list_.SetCount(static_cast<int>(shown_.size()));
            status_ = UiText("Row deleted");
        });
    };

    status_label_.translate = false;
    status_label_.flags = kLeft;
    status_label_.font = Font::Small;

    Refilter();
    list_.SetCount(static_cast<int>(shown_.size()));
    status_ = UiText("Ready");
}

auto DemoView::Title() const -> std::string { return UiText("Loot Filter Editor - controls"); }

void DemoView::Refilter() {
    shown_.clear();
    const text::ParsedQuery query = text::ParseQuery(search_.Value());
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        if (query.empty() || text::Matches(query, searchable_[static_cast<std::size_t>(i)])) shown_.push_back(i);
    }
    if (sort_ != Sort::Number) {
        std::stable_sort(shown_.begin(), shown_.end(), [this](int a, int b) {
            const std::string& left = searchable_[static_cast<std::size_t>(a)];
            const std::string& right = searchable_[static_cast<std::size_t>(b)];
            // past the "rNNNN " id
            const std::string_view l = std::string_view(left).substr(std::min<std::size_t>(6, left.size()));
            const std::string_view r = std::string_view(right).substr(std::min<std::size_t>(6, right.size()));
            return sort_ == Sort::Name ? l < r : r < l;
        });
    }
}

void DemoView::SetStatus(std::string text, Ui& ui) {
    status_ = std::move(text);
    ui.Repaint();
}

void DemoView::SetSort(Sort sort, Ui& ui) {
    sort_ = sort;
    sortNumber_.selected = sort == Sort::Number;
    sortName_.selected = sort == Sort::Name;
    sortNameDown_.selected = sort == Sort::NameDown;
    Refilter();
    list_.SetCount(static_cast<int>(shown_.size()));
    list_.Select(0, nullptr);
    list_.SetScroll(0);
    SetStatus(UiText("Sorted"), ui);
}

void DemoView::Layout(const LayoutContext& c) {
    Measurer& m = c.measure;
    const Box& area = c.content;
    const int gap = m.S(8);
    const int tight = m.S(4);
    const int body = m.LineHeight(Font::Body);
    const int rowH = std::max(m.S(30), body + m.S(10));
    const int rightWidth = std::clamp(area.Width() * 38 / 100, m.S(280), m.S(440));
    const int listRight = area.right - rightWidth - gap;

    // The list column: search box, counter, rows (the wiki's left column).
    const int searchHeight = std::max(m.S(28), body + m.S(10));
    search_.box = Box{area.left, area.top, listRight, area.top + searchHeight};
    const int countTop = search_.box.bottom + tight;
    count_.box = Box{area.left, countTop, listRight, countTop + m.S(22)};
    count_.text = search_.Value().empty()
                      ? UiFormat("{0} rows", {std::to_string(shown_.size())})
                      : UiFormat("{0} of {1} rows match", {std::to_string(shown_.size()), std::to_string(rows_.size())});
    count_.color = m.GetTheme().dim;
    list_.box = Box{area.left, count_.box.bottom + gap, listRight, area.bottom};
    list_.rowHeight = std::max(m.S(26), m.LineHeight(Font::Bold) + m.S(8));
    list_.rowGap = tight;
    list_.SetCount(static_cast<int>(shown_.size()));

    // The controls column.
    controls_.box = Box{listRight + gap, area.top, area.right, area.bottom};
    const int inLeft = controls_.box.left + m.S(16);
    const int inRight = controls_.box.right - m.S(16);
    int y = controls_.box.top + m.S(8) + m.LineHeight(Font::Bold) + m.S(12);
    const auto next = [&](int height) {
        const Box row{inLeft, y, inRight, y + height};
        y += height + gap;
        return row;
    };
    ethereal_.box = next(rowH);
    sound_.box = next(rowH);
    const int labelWidth = std::max({m.TextWidth(UiText(qualityLabel_.text), Font::Body), m.TextWidth(UiText(levelLabel_.text), Font::Body),
                                     m.TextWidth(UiText(noteLabel_.text), Font::Body)}) + m.S(12);
    const auto labelled = [&](Label& label, Widget& widget, int widgetWidth) {
        const Box row = next(rowH);
        label.box = Box{row.left, row.top, row.left + labelWidth, row.bottom};
        widget.box = Box{row.left + labelWidth, row.top, widgetWidth > 0 ? std::min(row.right, row.left + labelWidth + widgetWidth) : row.right, row.bottom};
    };
    labelled(qualityLabel_, quality_, 0);
    labelled(levelLabel_, level_, m.S(150));
    labelled(noteLabel_, note_, 0);
    {
        const int chipHeight = std::max(m.S(26), m.LineHeight(Font::Small) + m.S(10));
        const Box row = next(chipHeight);
        const int sortWidth = m.TextWidth(UiText(sortLabel_.text), Font::Small) + m.S(12);
        sortLabel_.box = Box{row.left, row.top, row.left + sortWidth, row.bottom};
        int x = sortLabel_.box.right;
        for (Chip* chip : {&sortNumber_, &sortName_, &sortNameDown_}) {
            const int w = std::max(m.S(56), m.TextWidth(UiText(chip->label), Font::Small) + m.S(16));
            chip->box = Box{x, row.top, x + w, row.bottom};
            x += w + m.S(6);
        }
    }
    {
        const Box row = next(rowH);
        const int half = (row.Width() - gap) / 2;
        open_.box = Box{row.left, row.top, row.left + half, row.bottom};
        remove_.box = Box{row.right - half, row.top, row.right, row.bottom};
    }
    status_label_.text = status_;
    status_label_.color = m.GetTheme().dim;
    status_label_.box = Box{inLeft, y, inRight, controls_.box.bottom - m.S(12)};
    open_.enabled = list_.Selected() >= 0;
}

void DemoView::FocusList(std::vector<Widget*>& out) {
    out = {&search_, &count_, &list_, &controls_, &ethereal_, &sound_, &qualityLabel_, &quality_, &levelLabel_, &level_,
           &noteLabel_, &note_, &sortLabel_, &sortNumber_, &sortName_, &sortNameDown_, &open_, &remove_, &status_label_};
}

void DemoView::Paint(Canvas&) {}

void DemoView::HelpRows(const Widget* focused, std::vector<HelpRow>& out) const {
    if (focused == &list_) out.push_back({Glyph::None, "Ctrl+F", "Search"});
    out.push_back({Glyph::View, "", "Type a search"});
}

auto DemoView::Key(const KeyEvent& event, Ui& ui) -> bool {
    if (event.key == Key::Other && event.mods.ctrl && event.vk == 'F') {
        ui.Focus(&search_);
        search_.edit.SelectAll();
        ui.Repaint();
        return true;
    }
    return false;
}

auto DemoView::Pad(const PadEvent& event, Ui& ui) -> bool {
    if (event.button == pad::Button::Back) {  // straight to the search box with its keyboard
        ui.OpenKeyboard(search_);
        return true;
    }
    return false;
}

}  // namespace esrfe::overlay
