#include "new_filter_view.hpp"

#include <algorithm>
#include <initializer_list>
#include <utility>

#include "rule_draft.h"
#include "ui_text.hpp"

namespace esrfe::ui {

using namespace overlay::text_flags;
using overlay::Font;
using overlay::UiFormat;
using overlay::UiText;

namespace {

auto ToOverlay(const edit::Rgb3& c) -> overlay::Rgb {
    return overlay::Rgb{static_cast<std::uint8_t>(std::clamp(c[0], 0, 255)), static_cast<std::uint8_t>(std::clamp(c[1], 0, 255)),
                        static_cast<std::uint8_t>(std::clamp(c[2], 0, 255))};
}
auto FromUi(const ui::Rgb& c) -> edit::Rgb3 { return edit::Rgb3{c.r, c.g, c.b}; }
auto FromOverlay(const overlay::Rgb& c) -> edit::Rgb3 { return edit::Rgb3{c.r, c.g, c.b}; }

void PaintTick(Canvas& canvas, const Box& row, int indent, bool on) {
    const overlay::Theme& theme = canvas.GetTheme();
    const int size = canvas.S(14);
    const Box tick{row.left + indent, row.CentreY() - size / 2, row.left + indent + size, row.CentreY() - size / 2 + size};
    canvas.Fill(tick, theme.field);
    canvas.Frame(tick, theme.border);
    if (on) canvas.Fill(tick.Inflated(-canvas.S(3)), theme.gold);
}

const char* const kPickTargets[] = {"Border", "Background", "Light beam"};

}  // namespace

NewFilterView::NewFilterView(std::shared_ptr<Source> source, std::function<void(Ui&, const std::string&)> created)
    : source_(std::move(source)), created_(std::move(created)) {
    std::string error;
    ready_ = edit::LoadStarter(source_->Folders().starter, starter_, error);
    problem_.translate = false;
    problem_.font = Font::Small;
    problem_.flags = kLeft;
    problem_.useThemeColor = false;
    if (!ready_) {
        problem_.text = UiFormat("The new-filter starter could not be read: {0}", {error});
        starter_.steps = {{"name", "Name", ""}};
    }
    recipe_ = ready_ ? edit::DefaultRecipe(starter_, false) : edit::Recipe{};
    if (recipe_.levels.empty()) recipe_.levels = {"Level 1"};

    for (std::size_t i = 0; i < starter_.steps.size(); ++i) {
        overlay::Chip& chip = stepChips_.emplace_back();
        chip.label = std::to_string(i + 1) + " " + starter_.steps[i].title;
        chip.onClick = [this, i](Ui& ui) {
            GoTo(static_cast<int>(i));
            ui.Repaint();
        };
    }
    heading_.font = Font::Bold;
    heading_.useThemeColor = false;
    hint_.font = Font::Small;
    hint_.flags = kLeft;
    hint_.useThemeColor = false;
    hint_.translate = false;

    // Name.
    nameLabel_.text = "Name (its file name too)";
    authorLabel_.text = "Author";
    startLabel_.text = "Start from";
    for (overlay::Label* l : {&nameLabel_, &authorLabel_, &startLabel_}) l->font = Font::Small;
    if (ready_) {  // "My filter", or "My filter 2" and so on when taken
        std::string why;
        std::string name = "My filter";
        for (int n = 2; n < 100 && !edit::CheckVersionName(source_->Folders(), name, why); ++n) name = "My filter " + std::to_string(n);
        name_.SetValue(name);
    }
    name_.placeholder = "My filter";
    name_.onChange = [this](const std::string& text, Ui& ui) {
        recipe_.name = text;
        Changed();
        ui.Repaint();
    };
    author_.placeholder = "Your name";
    author_.onChange = [this](const std::string& text, Ui& ui) {
        recipe_.author = text;
        Changed();
        ui.Repaint();
    };
    recipe_.name = name_.Value();
    for (int i = 0; i < 2; ++i) {
        overlay::Chip& chip = start_.emplace_back();
        chip.label = i == 0 ? "Recommended: everything Sully's filter does" : "Minimal: always show the valuables, hide little";
        chip.font = Font::Body;
        chip.onClick = [this, i](Ui& ui) {
            const std::string name = recipe_.name, author = recipe_.author;
            const std::vector<std::string> levels = recipe_.levels;
            const int def = recipe_.defaultLevel;
            recipe_ = edit::DefaultRecipe(starter_, i == 1);
            recipe_.name = name;
            recipe_.author = author;
            recipe_.levels = levels;
            recipe_.defaultLevel = def;
            Changed();
            ui.Repaint();
        };
    }

    // Levels.
    countLabel_.text = "How many strictness levels";
    defaultLabel_.text = "The level a new character starts on";
    for (overlay::Label* l : {&countLabel_, &defaultLabel_}) l->font = Font::Small;
    for (int i = 1; i <= 5; ++i) {
        overlay::Chip& chip = counts_.emplace_back();
        chip.label = std::to_string(i);
        chip.translate = false;
        chip.onClick = [this, i](Ui& ui) {
            SetLevelCount(i);
            ui.Repaint();
        };
    }
    for (int i = 0; i < 5; ++i) {
        overlay::Label& l = levelLabels_.emplace_back();
        l.text = UiFormat("Level {0}", {std::to_string(i + 1)});
        l.translate = false;
        l.font = Font::Small;
        levelNames_.emplace_back();
    }
    for (std::size_t i = 0; i < levelNames_.size(); ++i) {
        levelNames_[i].onChange = [this, i](const std::string& text, Ui& ui) {
            if (i < recipe_.levels.size()) recipe_.levels[i] = text;
            Changed();
            ui.Repaint();
        };
    }
    defaultLevel_.translate = false;
    defaultLevel_.onChange = [this](int index, Ui& ui) {
        recipe_.defaultLevel = index + 1;
        Changed();
        ui.Repaint();
    };

    // Parts.
    list_.rowGap = 0;
    list_.paintRow = [this](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(lines_.size())) return;
        const Line& line = lines_[static_cast<std::size_t>(index)];
        const edit::StarterPart& part = starter_.parts[static_cast<std::size_t>(line.part)];
        const overlay::Theme& theme = canvas.GetTheme();
        const edit::PartChoice* choice = recipe_.Part(part.key);
        const bool partOn = choice == nullptr || choice->on;
        if (line.row < 0) {
            PaintTick(canvas, row, canvas.S(8), partOn);
            const Box text{row.left + canvas.S(30), row.top, row.right - canvas.S(6), row.bottom};
            canvas.PushClip(text);
            canvas.Text(part.label, text, partOn ? theme.headerGold : theme.dim, Font::Bold, kVCenter | kSingleLine);
            canvas.PopClip();
            return;
        }
        const edit::Row& r = choice->rows[static_cast<std::size_t>(line.row)];
        PaintTick(canvas, row, canvas.S(30), r.on && partOn);
        const Box text{row.left + canvas.S(52), row.top, row.right - canvas.S(6), row.bottom};
        canvas.PushClip(text);
        canvas.Text(LineText(index), text, r.on && partOn ? theme.text : theme.dim, Font::Small, kVCenter | kSingleLine);
        canvas.PopClip();
    };
    list_.onSelect = [this](int, Ui& ui) {
        SyncControls();
        ShowStep();
        ui.Repaint();
    };
    list_.onClick = [this](int index, Ui& ui) {
        list_.Select(index, nullptr);
        SyncControls();
        ShowStep();
        ui.Repaint();
    };
    list_.onActivate = [this](int index, Ui& ui) { ToggleLine(index, ui); };
    partTitle_.translate = false;
    partTitle_.font = Font::Bold;
    partTitle_.flags = kLeft;
    partTitle_.useThemeColor = false;
    partHelp_.translate = false;
    partHelp_.font = Font::Small;
    partHelp_.flags = kLeft;
    partHelp_.useThemeColor = false;
    partOn_.label = "This part is on";
    partOn_.onChange = [this](bool on, Ui& ui) {
        const Line* line = SelectedLine();
        if (line == nullptr) return;
        if (edit::PartChoice* c = PartChoice(line->part)) c->on = on;
        Changed();
        ui.Repaint();
    };
    rowOn_.label = "This step is on";
    rowOn_.onChange = [this](bool on, Ui& ui) {
        const Line* line = SelectedLine();
        if (line == nullptr) return;
        if (edit::Row* r = RowAt(*line)) r->on = on;
        Changed();
        ui.Repaint();
    };
    fromLabel_.text = "From level";
    toLabel_.text = "To level";
    clvlLabel_.text = "From character level (0: any)";
    amountLabel_.text = "Gold piles up to";
    for (overlay::Label* l : {&fromLabel_, &toLabel_, &clvlLabel_, &amountLabel_}) l->font = Font::Small;
    from_.translate = false;
    to_.translate = false;
    from_.onChange = [this](int index, Ui& ui) {
        const Line* line = SelectedLine();
        if (line == nullptr) return;
        if (edit::Row* r = RowAt(*line)) {
            r->from = index + 1;
            const int to = r->to <= 0 ? LevelCount() : r->to;
            if (to < r->from) r->to = r->from >= LevelCount() ? 0 : r->from;
        }
        Changed();
        ui.Repaint();
    };
    to_.onChange = [this](int index, Ui& ui) {
        const Line* line = SelectedLine();
        if (line == nullptr) return;
        if (edit::Row* r = RowAt(*line)) {
            r->to = index + 1 >= LevelCount() ? 0 : index + 1;
            if (index + 1 < r->from) r->from = index + 1;
        }
        Changed();
        ui.Repaint();
    };
    clvl_.minimum = 0;
    clvl_.maximum = 99;
    clvl_.step = 5;
    clvl_.onChange = [this](int v, Ui& ui) {
        const Line* line = SelectedLine();
        if (line == nullptr) return;
        if (edit::Row* r = RowAt(*line)) r->clvl = v;
        Changed();
        ui.Repaint();
    };
    amount_.minimum = 0;
    amount_.maximum = 1000000;
    amount_.step = 50;
    amount_.onChange = [this](int v, Ui& ui) {
        const Line* line = SelectedLine();
        if (line == nullptr) return;
        if (edit::Row* r = RowAt(*line); r != nullptr && r->amount >= 0) r->amount = v;
        Changed();
        ui.Repaint();
    };
    towns_.label = "In towns too";
    towns_.onChange = [this](bool on, Ui& ui) {
        const Line* line = SelectedLine();
        if (line == nullptr) return;
        if (edit::Row* r = RowAt(*line)) r->towns = on;
        Changed();
        ui.Repaint();
    };
    addStep_.label = "+ Add a step like this";
    addStep_.onClick = [this](Ui& ui) {
        const Line* line = SelectedLine();
        if (line == nullptr || line->row < 0) return;
        edit::PartChoice* c = PartChoice(line->part);
        if (c == nullptr) return;
        edit::Row copy = c->rows[static_cast<std::size_t>(line->row)];
        if (copy.copyOf.empty()) copy.copyOf = copy.id;
        copy.id = recipe_.NextAddedId();
        copy.on = true;
        const int part = line->part;
        const std::size_t at = static_cast<std::size_t>(line->row) + 1;
        c->rows.insert(c->rows.begin() + static_cast<std::ptrdiff_t>(at), copy);
        Changed();
        for (int i = 0; i < static_cast<int>(lines_.size()); ++i)
            if (lines_[static_cast<std::size_t>(i)].part == part && lines_[static_cast<std::size_t>(i)].row == static_cast<int>(at)) list_.Select(i, nullptr);
        SyncControls();
        ShowStep();
        ui.Repaint();
    };
    removeStep_.label = "Remove this step";
    removeStep_.onClick = [this](Ui& ui) {
        const Line* line = SelectedLine();
        if (line == nullptr || line->row < 0) return;
        edit::PartChoice* c = PartChoice(line->part);
        if (c == nullptr || c->rows[static_cast<std::size_t>(line->row)].copyOf.empty()) return;
        c->rows.erase(c->rows.begin() + line->row);
        Changed();
        ui.Repaint();
    };

    // Look.
    styles_.rowGap = 0;
    styles_.SetCount(static_cast<int>(starter_.looks.size()));
    styles_.paintRow = [this](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
        if (index < 0 || index >= static_cast<int>(starter_.looks.size())) return;
        const edit::StarterLook& look = starter_.looks[static_cast<std::size_t>(index)];
        const overlay::Theme& theme = canvas.GetTheme();
        const bool changed = std::as_const(recipe_).Look(look.style) != nullptr;
        canvas.Text(look.label + (changed ? "  *" : ""), Box{row.left + canvas.S(10), row.top, row.right - canvas.S(6), row.bottom},
                    changed ? theme.gold : theme.text, Font::Body, kVCenter | kSingleLine);
    };
    styles_.onSelect = [this](int, Ui& ui) {
        SyncControls();
        ui.Repaint();
    };
    styles_.onClick = [this](int index, Ui& ui) {
        styles_.Select(index, nullptr);
        SyncControls();
        ui.Repaint();
    };
    if (!starter_.looks.empty()) styles_.Select(0, nullptr);
    borderLabel_.text = "Border";
    backLabel_.text = "Background";
    beamLabel_.text = "Light beam";
    pickLabel_.text = "Any colour for:";
    volumeLabel_.text = "Filter volume";
    for (overlay::Label* l : {&borderLabel_, &backLabel_, &beamLabel_, &pickLabel_, &volumeLabel_}) l->font = Font::Small;
    for (overlay::Chip* c : {&borderSame_, &backSame_, &beamSame_}) c->label = "As it is";
    borderSame_.onClick = [this](Ui& ui) { SetLook(0, nullptr); ui.Repaint(); };
    backSame_.onClick = [this](Ui& ui) { SetLook(1, nullptr); ui.Repaint(); };
    beamSame_.onClick = [this](Ui& ui) { SetLook(2, nullptr); ui.Repaint(); };
    for (const NamedColor& c : Swatches()) {
        const edit::Rgb3 rgb = FromUi(c.rgb);
        for (int what = 0; what < 3; ++what) {
            std::vector<overlay::Swatch>& row = what == 0 ? borderSw_ : what == 1 ? backSw_ : beamSw_;
            overlay::Swatch& s = row.emplace_back();
            s.label = c.english;
            s.color = ToOverlay(rgb);
            s.onClick = [this, what, rgb](Ui& ui) {
                SetLook(what, &rgb);
                pickWhat_ = what;
                SyncControls();
                ui.Repaint();
            };
        }
    }
    for (int i = 0; i < 3; ++i) {
        overlay::Chip& chip = pickFor_.emplace_back();
        chip.label = kPickTargets[i];
        chip.onClick = [this, i](Ui& ui) {
            pickWhat_ = i;
            SyncControls();
            ui.Repaint();
        };
    }
    picker_.onChange = [this](overlay::Rgb color, Ui& ui) {
        const edit::Rgb3 c = FromOverlay(color);
        SetLook(pickWhat_, &c);
        ui.Repaint();
    };
    volume_.minimum = 0;
    volume_.maximum = 100;
    volume_.step = 5;
    volume_.onChange = [this](int v, Ui& ui) {
        recipe_.soundVolume = v;
        volumeValue_.text = std::to_string(v);
        ui.Repaint();
    };
    volume_.onCommit = [this](int, Ui&) { PlayTest(); };
    volumeValue_.translate = false;
    volumeValue_.font = Font::Small;
    test_.label = "Test";
    test_.onClick = [this](Ui&) { PlayTest(); };

    // Check.
    checkList_.drawRowBackground = false;
    checkList_.rowGap = 0;
    checkList_.paintRow = [this](Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
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

    // Footer.
    back_.label = "< Back";
    back_.onClick = [this](Ui& ui) {
        if (step_ == 0) {
            ui.PopView();
            return;
        }
        GoTo(step_ - 1);
        ui.Repaint();
    };
    next_.label = "Next >";
    next_.onClick = [this](Ui& ui) {
        if (step_ + 1 < StepCount()) GoTo(step_ + 1);
        ui.Repaint();
    };
    create_.label = "Create the filter";
    create_.style = overlay::Button::Style::Primary;
    create_.onClick = [this](Ui& ui) { Save(ui); };

    SetLevelCount(LevelCount());
    GoTo(0);
}

auto NewFilterView::Title() const -> std::string { return UiText("New filter"); }

auto NewFilterView::StepKey() const -> std::string {
    return step_ >= 0 && step_ < StepCount() ? starter_.steps[static_cast<std::size_t>(step_)].key : std::string{};
}

void NewFilterView::GoTo(int step) {
    step_ = std::clamp(step, 0, std::max(0, StepCount() - 1));
    RebuildLines();
    list_.SetScroll(0);
    list_.Select(lines_.empty() ? -1 : 0, nullptr);
    if (StepKey() == "check") RebuildCheck();
    Changed();
}

auto NewFilterView::PartChoice(int part) -> edit::PartChoice* {
    if (part < 0 || part >= static_cast<int>(starter_.parts.size())) return nullptr;
    return recipe_.Part(starter_.parts[static_cast<std::size_t>(part)].key);
}

auto NewFilterView::SelectedLine() const -> const Line* {
    const int i = list_.Selected();
    return i >= 0 && i < static_cast<int>(lines_.size()) ? &lines_[static_cast<std::size_t>(i)] : nullptr;
}

auto NewFilterView::RowAt(const Line& line) -> edit::Row* {
    edit::PartChoice* c = PartChoice(line.part);
    if (c == nullptr || line.row < 0 || line.row >= static_cast<int>(c->rows.size())) return nullptr;
    return &c->rows[static_cast<std::size_t>(line.row)];
}

void NewFilterView::RebuildLines() {
    lines_.clear();
    const std::string key = StepKey();
    for (std::size_t p = 0; p < starter_.parts.size(); ++p) {
        if (starter_.parts[p].step != key) continue;
        lines_.push_back(Line{static_cast<int>(p), -1});
        const edit::PartChoice* c = recipe_.Part(starter_.parts[p].key);
        if (c == nullptr) continue;
        for (std::size_t r = 0; r < c->rows.size(); ++r) lines_.push_back(Line{static_cast<int>(p), static_cast<int>(r)});
    }
    list_.SetCount(static_cast<int>(lines_.size()));
}

auto NewFilterView::RuleTitle(const std::string& id) const -> std::string {
    const int i = starter_.filter.FindRule(id);
    if (i < 0) return id;
    std::string title = starter_.filter.Rules()[static_cast<std::size_t>(i)].title;
    const std::size_t a = title.find('|');
    const std::size_t b = title.rfind('|');
    if (a != std::string::npos && b != std::string::npos && b > a) title = title.substr(a + 2, b - a - 3);
    else if (a != std::string::npos) title = title.substr(a + 2);
    return title;
}

auto NewFilterView::RowSummary(const edit::Row& row, const edit::StarterPart& part) const -> std::string {
    const int levels = LevelCount();
    const int to = row.to <= 0 || row.to > levels ? levels : row.to;
    std::string text;
    if (row.from > to) text = UiText("not on any level of this filter");
    else if (row.from <= 1 && to >= levels) text = UiText("every level");
    else if (row.from == to) text = UiFormat("level {0}", {std::to_string(row.from)});
    else text = UiFormat("levels {0}-{1}", {std::to_string(row.from), std::to_string(to)});
    if (row.amount >= 0) text = UiFormat("up to {0} gold", {std::to_string(row.amount)}) + "  ·  " + text;
    if (row.clvl > 0) text += "  ·  " + UiFormat("from character level {0}", {std::to_string(row.clvl)});
    if (part.kind == edit::StarterPart::Kind::Hide) text += "  ·  " + (row.towns ? UiText("in towns too") : UiText("outside towns"));
    return text;
}

auto NewFilterView::LineText(int index) const -> std::string {
    if (index < 0 || index >= static_cast<int>(lines_.size())) return {};
    const Line& line = lines_[static_cast<std::size_t>(index)];
    const edit::StarterPart& part = starter_.parts[static_cast<std::size_t>(line.part)];
    if (line.row < 0) return part.label;
    const edit::PartChoice* c = recipe_.Part(part.key);
    const edit::Row& r = c->rows[static_cast<std::size_t>(line.row)];
    return RowSummary(r, part) + "  —  " + RuleTitle(r.copyOf.empty() ? r.id : r.copyOf) + (r.copyOf.empty() ? "" : UiText(" (your step)"));
}

void NewFilterView::ToggleLine(int index, Ui& ui) {
    if (index < 0 || index >= static_cast<int>(lines_.size())) return;
    const Line line = lines_[static_cast<std::size_t>(index)];
    list_.Select(index, nullptr);
    if (line.row < 0) {
        if (edit::PartChoice* c = PartChoice(line.part)) c->on = !c->on;
    } else if (edit::Row* r = RowAt(line)) {
        r->on = !r->on;
    }
    Changed();
    ui.Repaint();
}

void NewFilterView::SetLevelCount(int count) {
    count = std::clamp(count, 1, 5);
    const std::vector<std::string> suggested = edit::DefaultRecipe(starter_, false).levels;
    while (static_cast<int>(recipe_.levels.size()) < count) {
        const std::size_t i = recipe_.levels.size();
        recipe_.levels.push_back(i < suggested.size() ? suggested[i] : UiFormat("Level {0}", {std::to_string(i + 1)}));
    }
    recipe_.levels.resize(static_cast<std::size_t>(count));
    recipe_.defaultLevel = std::clamp(recipe_.defaultLevel, 1, count);
    for (std::size_t i = 0; i < levelNames_.size(); ++i) levelNames_[i].SetValue(i < recipe_.levels.size() ? recipe_.levels[i] : std::string{});
    Changed();
}

auto NewFilterView::CurrentLook() const -> edit::LookChoice {
    const int i = styles_.Selected();
    if (i < 0 || i >= static_cast<int>(starter_.looks.size())) return {};
    const std::string& style = starter_.looks[static_cast<std::size_t>(i)].style;
    edit::LookChoice look = edit::StarterLookOf(starter_, style);
    if (const edit::LookChoice* mine = recipe_.Look(style)) {
        if (mine->border) look.border = mine->border;
        if (mine->background) look.background = mine->background;
        if (mine->beam) look.beam = mine->beam;
    }
    return look;
}

void NewFilterView::SetLook(int what, const edit::Rgb3* color) {
    const int i = styles_.Selected();
    if (i < 0 || i >= static_cast<int>(starter_.looks.size())) return;
    edit::LookChoice& look = recipe_.Look(starter_.looks[static_cast<std::size_t>(i)].style);
    std::optional<edit::Rgb3>& slot = what == 0 ? look.border : what == 1 ? look.background : look.beam;
    if (color == nullptr) slot.reset();
    else slot = *color;
    if (!look.border && !look.background && !look.beam)
        recipe_.looks.erase(std::remove_if(recipe_.looks.begin(), recipe_.looks.end(), [&](const edit::LookChoice& l) { return l.style == look.style; }),
                            recipe_.looks.end());
    Changed();
}

void NewFilterView::PlayTest() {
    const std::string path = starter_.folder + "/sounds/unique.mp3";
    source_->PlaySound(path, source_->FileSoundVolume() * recipe_.soundVolume / 100);
}

void NewFilterView::RebuildCheck() {
    checkText_.clear();
    std::string text, error;
    checkText_.push_back("#" + UiText("The filter"));
    checkText_.push_back(UiFormat("Name: {0}", {recipe_.name}));
    if (!recipe_.author.empty()) checkText_.push_back(UiFormat("Author: {0}", {recipe_.author}));
    std::string levels;
    for (std::size_t i = 0; i < recipe_.levels.size(); ++i) levels += (i > 0 ? ", " : "") + std::to_string(i + 1) + " " + recipe_.levels[i];
    checkText_.push_back(UiFormat("Levels: {0} (a new character starts on {1})", {levels, std::to_string(recipe_.defaultLevel)}));
    checkText_.push_back(UiFormat("Sound volume: {0}", {std::to_string(recipe_.soundVolume)}));
    for (const edit::StarterStep& step : starter_.steps) {
        int on = 0, total = 0;
        std::vector<std::string> off;
        for (const edit::StarterPart& p : starter_.parts) {
            if (p.step != step.key) continue;
            ++total;
            const edit::PartChoice* c = recipe_.Part(p.key);
            if (c == nullptr || c->on) ++on;
            else off.push_back(p.label);
        }
        if (total == 0) continue;
        checkText_.push_back("#" + step.title);
        checkText_.push_back(UiFormat("{0} of {1} parts on", {std::to_string(on), std::to_string(total)}));
        for (const std::string& o : off) checkText_.push_back(UiFormat("Off: {0}", {o}));
    }
    if (!recipe_.looks.empty()) {
        checkText_.push_back("#" + UiText("Label colours"));
        for (const edit::LookChoice& l : recipe_.looks)
            for (const edit::StarterLook& s : starter_.looks)
                if (s.style == l.style) checkText_.push_back(UiFormat("Your colours: {0}", {s.label}));
    }
    if (ready_ && edit::MakeFilter(starter_, recipe_, true, text, error)) {
        document::Document doc;
        std::vector<document::Diag> diags;
        if (document::Document::Parse(text, doc, diags))
            checkText_.push_back("#" + UiFormat("{0} rules", {std::to_string(doc.ActiveCount())}));
    } else if (!error.empty()) {
        checkText_.push_back("#" + UiFormat("It cannot be made: {0}", {error}));
    }
    checkList_.SetCount(static_cast<int>(checkText_.size()));
}

void NewFilterView::Changed() {
    if (StepKey() == "check") RebuildCheck();
    // Lines stay where they are; a step added or removed changes them.
    const int selected = list_.Selected();
    RebuildLines();
    list_.Select(std::min(selected, static_cast<int>(lines_.size()) - 1), nullptr);
    std::string why;
    if (StepKey() == "name" || StepKey() == "check")
        problem_.text = ready_ && !edit::CheckVersionName(source_->Folders(), recipe_.name, why) ? UiText(why) : (ready_ ? std::string{} : problem_.text);
    SyncControls();
    ShowStep();
}

void NewFilterView::SyncControls() {
    for (int i = 0; i < static_cast<int>(stepChips_.size()); ++i) stepChips_[static_cast<std::size_t>(i)].selected = i == step_;
    start_[0].selected = !recipe_.minimal;
    start_[1].selected = recipe_.minimal;
    for (int i = 0; i < 5; ++i) counts_[static_cast<std::size_t>(i)].selected = LevelCount() == i + 1;
    defaultLevel_.options.clear();
    for (std::size_t i = 0; i < recipe_.levels.size(); ++i) defaultLevel_.options.push_back(std::to_string(i + 1) + " " + recipe_.levels[i]);
    defaultLevel_.selected = recipe_.defaultLevel - 1;
    std::vector<std::string> levelChoices;
    for (std::size_t i = 0; i < recipe_.levels.size(); ++i) levelChoices.push_back(std::to_string(i + 1) + " " + recipe_.levels[i]);
    from_.options = levelChoices;
    to_.options = levelChoices;

    const Line* line = SelectedLine();
    if (line != nullptr) {
        const edit::StarterPart& part = starter_.parts[static_cast<std::size_t>(line->part)];
        const edit::PartChoice* c = recipe_.Part(part.key);
        partOn_.on = c == nullptr || c->on;
        partTitle_.text = part.label;
        partHelp_.text = part.help;
        if (const edit::Row* r = RowAt(*line)) {
            rowOn_.on = r->on;
            from_.selected = std::clamp(r->from, 1, LevelCount()) - 1;
            to_.selected = (r->to <= 0 || r->to > LevelCount() ? LevelCount() : r->to) - 1;
            clvl_.SetValue(r->clvl);
            amount_.SetValue(static_cast<int>(std::max<std::int64_t>(0, r->amount)));
            towns_.on = r->towns;
            partTitle_.text = RuleTitle(r->copyOf.empty() ? r->id : r->copyOf);
            partHelp_.text = RowSummary(*r, part);
        }
    }
    const edit::LookChoice look = CurrentLook();
    for (std::size_t i = 0; i < Swatches().size(); ++i) {
        const edit::Rgb3 rgb = FromUi(Swatches()[i].rgb);
        borderSw_[i].selected = look.border && *look.border == rgb;
        backSw_[i].selected = look.background && *look.background == rgb;
        beamSw_[i].selected = look.beam && *look.beam == rgb;
    }
    const int i = styles_.Selected();
    const edit::LookChoice* mine = i >= 0 && i < static_cast<int>(starter_.looks.size()) ? std::as_const(recipe_).Look(starter_.looks[static_cast<std::size_t>(i)].style) : nullptr;
    borderSame_.selected = mine == nullptr || !mine->border;
    backSame_.selected = mine == nullptr || !mine->background;
    beamSame_.selected = mine == nullptr || !mine->beam;
    for (int p = 0; p < 3; ++p) pickFor_[static_cast<std::size_t>(p)].selected = pickWhat_ == p;
    const std::optional<edit::Rgb3>& pick = pickWhat_ == 0 ? look.border : pickWhat_ == 1 ? look.background : look.beam;
    picker_.SetColor(ToOverlay(pick.value_or(edit::Rgb3{255, 200, 0})));
    volume_.SetValue(recipe_.soundVolume);
    volumeValue_.text = std::to_string(recipe_.soundVolume);
    std::string why;
    create_.enabled = ready_ && edit::CheckVersionName(source_->Folders(), recipe_.name, why);
    next_.visible = step_ + 1 < StepCount();
    create_.visible = StepKey() == "check";
}

void NewFilterView::ShowStep() {
    const std::string key = StepKey();
    const auto show = [](bool on, std::initializer_list<Widget*> widgets) {
        for (Widget* w : widgets) w->visible = on;
    };
    show(key == "name", {&nameLabel_, &name_, &authorLabel_, &author_, &startLabel_});
    for (overlay::Chip& c : start_) c.visible = key == "name";
    show(key == "levels", {&countLabel_, &defaultLabel_, &defaultLevel_});
    for (overlay::Chip& c : counts_) c.visible = key == "levels";
    for (std::size_t i = 0; i < levelNames_.size(); ++i) {
        const bool on = key == "levels" && static_cast<int>(i) < LevelCount();
        levelNames_[i].visible = on;
        levelLabels_[i].visible = on;
    }
    const bool parts = !lines_.empty();
    const Line* line = SelectedLine();
    const edit::Row* row = line != nullptr ? const_cast<NewFilterView*>(this)->RowAt(*line) : nullptr;
    const edit::StarterPart* part = line != nullptr ? &starter_.parts[static_cast<std::size_t>(line->part)] : nullptr;
    show(parts, {&list_, &partTitle_, &partHelp_});
    show(parts && line != nullptr && line->row < 0, {&partOn_});
    show(parts && row != nullptr, {&rowOn_, &fromLabel_, &toLabel_, &from_, &to_, &clvlLabel_, &clvl_});
    show(parts && row != nullptr && row->amount >= 0, {&amountLabel_, &amount_});
    const bool hide = part != nullptr && part->kind == edit::StarterPart::Kind::Hide;
    show(parts && row != nullptr && hide, {&towns_, &addStep_});
    show(parts && row != nullptr && !row->copyOf.empty(), {&removeStep_});
    const bool look = key == "look";
    show(look, {&styles_, &borderLabel_, &backLabel_, &beamLabel_, &pickLabel_, &borderSame_, &backSame_, &beamSame_, &picker_, &volumeLabel_,
                &volume_, &volumeValue_, &test_});
    for (auto* r : {&borderSw_, &backSw_, &beamSw_})
        for (overlay::Swatch& s : *r) s.visible = look;
    for (overlay::Chip& c : pickFor_) c.visible = look;
    checkList_.visible = key == "check";
    const edit::StarterStep* step = step_ < StepCount() ? &starter_.steps[static_cast<std::size_t>(step_)] : nullptr;
    heading_.text = step != nullptr ? step->title : std::string{};
    heading_.translate = true;
    if (key == "name") hint_.text = UiText("Your own filter, made from Sully's QoL Filter's choices. Every step is filled in: change what you like, or go straight to Check.");
    else if (key == "levels") hint_.text = UiText("Strictness levels: players switch between them in game as they need less on the ground. Keep the names or type your own.");
    else if (key == "look") hint_.text = UiText("The label styles: choose one, then its border, background and light beam. The picture shows it.");
    else if (key == "check") hint_.text = UiText("Your filter in words. Create makes it and switches the game to it; it opens in the editor like any filter.");
    else hint_.text = step != nullptr ? step->help : std::string{};
}

void NewFilterView::Save(Ui& ui) {
    if (!ready_) return;
    std::string merged, error;
    if (!edit::CreateFilter(source_->Folders(), starter_, recipe_, merged, error)) {
        problem_.text = UiFormat("Not made: {0}", {error});
        ui.Repaint();
        return;
    }
    ui.PopView();
    if (created_) created_(ui, merged);
}

auto NewFilterView::InitialFocus() -> Widget* { return &name_; }

auto NewFilterView::Pad(const overlay::PadEvent& event, Ui& ui) -> bool {
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

void NewFilterView::Layout(const LayoutContext& c) {
    overlay::Measurer& m = c.measure;
    const Box& area = c.content;
    const overlay::Theme& theme = m.GetTheme();
    const int gap = m.S(8);
    const int chipH = std::max(m.S(28), m.LineHeight(Font::Small) + m.S(12));
    const int rowH = std::max(m.S(28), m.LineHeight(Font::Body) + m.S(8));
    const int buttonH = std::max(m.S(32), m.LineHeight(Font::Body) + m.S(12));
    const int fieldH = std::max(m.S(30), m.LineHeight(Font::Body) + m.S(10));
    const int small = m.LineHeight(Font::Small) + m.S(4);

    int x = area.left, y = area.top;
    for (overlay::Chip& chip : stepChips_) {
        const int w = m.TextWidth(UiText(chip.label), Font::Small) + m.S(18);
        if (x + w > area.right && x > area.left) {
            x = area.left;
            y += chipH + m.S(4);
        }
        chip.box = Box{x, y, x + w, y + chipH};
        x += w + m.S(4);
    }
    y += chipH + m.S(10);
    heading_.box = Box{area.left, y, area.right, y + m.LineHeight(Font::Bold) + m.S(4)};
    heading_.color = theme.headerGold;
    y = heading_.box.bottom;
    hint_.box = Box{area.left, y, area.right, y + m.LineHeight(Font::Small) * 2 + m.S(4)};
    hint_.color = theme.dim;
    y = hint_.box.bottom + gap;

    back_.box = Box{area.left, area.bottom - buttonH, area.left + m.S(120), area.bottom};
    const int createW = m.TextWidth(UiText(create_.label), Font::Body) + m.S(40);
    create_.box = Box{area.right - createW, area.bottom - buttonH, area.right, area.bottom};
    next_.box = Box{area.right - m.S(120), area.bottom - buttonH, area.right, area.bottom};
    if (create_.visible) next_.box = Box{};
    problem_.box = Box{back_.box.right + gap, area.bottom - buttonH, (create_.visible ? create_.box.left : next_.box.left) - gap, area.bottom};
    problem_.color = theme.danger;
    const Box body{area.left, y, area.right, area.bottom - buttonH - gap};
    previewBox_ = Box{};
    const std::string key = StepKey();

    if (key == "name") {
        int cy = body.top;
        const int w = std::min(body.Width(), m.S(560));
        for (auto [label, box] : {std::pair<overlay::Label*, overlay::TextBox*>{&nameLabel_, &name_}, {&authorLabel_, &author_}}) {
            label->box = Box{body.left, cy, body.left + w, cy + small};
            cy += small;
            box->box = Box{body.left, cy, body.left + w, cy + fieldH};
            cy += fieldH + gap;
        }
        startLabel_.box = Box{body.left, cy, body.left + w, cy + small};
        cy += small;
        for (overlay::Chip& chip : start_) {
            chip.box = Box{body.left, cy, body.left + w, cy + buttonH};
            cy += buttonH + m.S(6);
        }
    } else if (key == "levels") {
        int cy = body.top;
        countLabel_.box = Box{body.left, cy, body.right, cy + small};
        cy += small;
        int cx = body.left;
        for (overlay::Chip& chip : counts_) {
            chip.box = Box{cx, cy, cx + m.S(44), cy + chipH};
            cx += m.S(48);
        }
        cy += chipH + gap;
        const int w = std::min(body.Width(), m.S(480));
        for (int i = 0; i < LevelCount(); ++i) {
            levelLabels_[static_cast<std::size_t>(i)].box = Box{body.left, cy, body.left + m.S(80), cy + fieldH};
            levelNames_[static_cast<std::size_t>(i)].box = Box{body.left + m.S(84), cy, body.left + m.S(84) + w, cy + fieldH};
            cy += fieldH + m.S(4);
        }
        cy += gap;
        defaultLabel_.box = Box{body.left, cy, body.right, cy + small};
        cy += small;
        defaultLevel_.box = Box{body.left, cy, body.left + std::min(w, m.S(360)), cy + fieldH};
    } else if (key == "look") {
        const int leftW = std::clamp(body.Width() * 26 / 100, m.S(180), m.S(280));
        styles_.box = Box{body.left, body.top, body.left + leftW, body.bottom};
        styles_.rowHeight = rowH;
        const int mid = styles_.box.right + gap * 2;
        const int midRight = mid + (body.right - mid) * 55 / 100;
        const int swH = std::max(m.S(24), m.LineHeight(Font::Small) + m.S(8));
        int cy = body.top;
        const auto row = [&](overlay::Label& label, overlay::Chip& same, std::vector<overlay::Swatch>& swatches) {
            label.box = Box{mid, cy, midRight, cy + small};
            cy += small;
            int cx = mid;
            same.box = Box{cx, cy, cx + m.S(76), cy + swH};
            cx += m.S(80);
            const int count = static_cast<int>(swatches.size());
            const int w = count > 0 ? std::max(m.S(18), (midRight - cx - m.S(3) * (count - 1)) / count) : 0;
            for (overlay::Swatch& s : swatches) {
                s.box = Box{cx, cy, cx + w, cy + swH};
                cx += w + m.S(3);
            }
            cy += swH + gap;
        };
        row(borderLabel_, borderSame_, borderSw_);
        row(backLabel_, backSame_, backSw_);
        row(beamLabel_, beamSame_, beamSw_);
        pickLabel_.box = Box{mid, cy, midRight, cy + small};
        cy += small;
        int cx = mid;
        for (overlay::Chip& chip : pickFor_) {
            const int w = m.TextWidth(UiText(chip.label), Font::Small) + m.S(18);
            chip.box = Box{cx, cy, cx + w, cy + chipH};
            cx += w + m.S(4);
        }
        cy += chipH + gap;
        picker_.box = Box{mid, cy, midRight, std::max(cy + m.S(120), body.bottom - fieldH - small - gap)};
        cy = picker_.box.bottom + gap;
        volumeLabel_.box = Box{mid, cy, mid + m.S(110), cy + fieldH};
        volume_.box = Box{mid + m.S(110), cy, midRight - m.S(130), cy + fieldH};
        volumeValue_.box = Box{volume_.box.right + m.S(4), cy, volume_.box.right + m.S(44), cy + fieldH};
        volumeValue_.color = theme.text;
        test_.box = Box{midRight - m.S(80), cy, midRight, cy + fieldH};
        previewBox_ = Box{midRight + gap * 2, body.top, body.right, body.top + std::min(body.Height(), m.S(220))};
    } else if (key == "check") {
        checkList_.box = body;
        checkList_.rowHeight = std::max(m.S(24), m.LineHeight(Font::Bold) + m.S(6));
    } else if (!lines_.empty()) {
        const int split = body.left + body.Width() * 60 / 100;
        list_.box = Box{body.left, body.top, split - gap, body.bottom};
        list_.rowHeight = rowH;
        const int px = split + gap;
        int cy = body.top;
        partTitle_.box = Box{px, cy, body.right, cy + m.LineHeight(Font::Bold) * 2 + m.S(4)};
        partTitle_.color = theme.headerGold;
        cy = partTitle_.box.bottom;
        partHelp_.box = Box{px, cy, body.right, cy + m.LineHeight(Font::Small) * 3 + m.S(4)};
        partHelp_.color = theme.dim;
        cy = partHelp_.box.bottom + gap;
        partOn_.box = Box{px, cy, body.right, cy + rowH};
        rowOn_.box = Box{px, cy, body.right, cy + rowH};
        cy += rowH + gap;
        const int half = (body.right - px - gap) / 2;
        fromLabel_.box = Box{px, cy, px + half, cy + small};
        toLabel_.box = Box{px + half + gap, cy, body.right, cy + small};
        cy += small;
        from_.box = Box{px, cy, px + half, cy + fieldH};
        to_.box = Box{px + half + gap, cy, body.right, cy + fieldH};
        cy += fieldH + gap;
        clvlLabel_.box = Box{px, cy, body.right, cy + small};
        cy += small;
        clvl_.box = Box{px, cy, px + m.S(170), cy + fieldH};
        cy += fieldH + gap;
        if (amount_.visible) {
            amountLabel_.box = Box{px, cy, body.right, cy + small};
            cy += small;
            amount_.box = Box{px, cy, px + m.S(200), cy + fieldH};
            cy += fieldH + gap;
        }
        towns_.box = Box{px, cy, body.right, cy + rowH};
        cy += rowH + gap;
        addStep_.box = Box{px, cy, body.right, cy + buttonH};
        cy += buttonH + m.S(6);
        removeStep_.box = Box{px, cy, body.right, cy + buttonH};
    }
}

void NewFilterView::FocusList(std::vector<Widget*>& out) {
    out.clear();
    for (overlay::Chip& c : stepChips_) out.push_back(&c);
    for (Widget* w : std::initializer_list<Widget*>{&heading_, &hint_, &nameLabel_, &name_, &authorLabel_, &author_, &startLabel_}) out.push_back(w);
    for (overlay::Chip& c : start_) out.push_back(&c);
    out.push_back(&countLabel_);
    for (overlay::Chip& c : counts_) out.push_back(&c);
    for (std::size_t i = 0; i < levelNames_.size(); ++i) {
        out.push_back(&levelLabels_[i]);
        out.push_back(&levelNames_[i]);
    }
    for (Widget* w : std::initializer_list<Widget*>{&defaultLabel_, &defaultLevel_, &list_, &partTitle_, &partHelp_, &partOn_, &rowOn_, &fromLabel_, &from_,
                                                    &toLabel_, &to_, &clvlLabel_, &clvl_, &amountLabel_, &amount_, &towns_, &addStep_, &removeStep_,
                                                    &styles_, &borderLabel_, &borderSame_})
        out.push_back(w);
    for (overlay::Swatch& s : borderSw_) out.push_back(&s);
    out.push_back(&backLabel_);
    out.push_back(&backSame_);
    for (overlay::Swatch& s : backSw_) out.push_back(&s);
    out.push_back(&beamLabel_);
    out.push_back(&beamSame_);
    for (overlay::Swatch& s : beamSw_) out.push_back(&s);
    out.push_back(&pickLabel_);
    for (overlay::Chip& c : pickFor_) out.push_back(&c);
    for (Widget* w : std::initializer_list<Widget*>{&picker_, &volumeLabel_, &volume_, &volumeValue_, &test_, &checkList_, &problem_, &back_, &next_, &create_})
        out.push_back(w);
}

void NewFilterView::Paint(Canvas& canvas) {
    if (StepKey() != "look" || previewBox_.Width() <= 0) return;
    const int i = styles_.Selected();
    if (i < 0 || i >= static_cast<int>(starter_.looks.size())) return;
    const overlay::Theme& theme = canvas.GetTheme();
    const Box& p = previewBox_;
    canvas.Fill(p, overlay::Rgb{22, 20, 18});
    canvas.Frame(p, theme.border);
    const edit::LookChoice look = CurrentLook();
    const std::string name = starter_.looks[static_cast<std::size_t>(i)].label;
    const int labelW = canvas.TextWidth(name, Font::Bold) + canvas.S(20);
    const int labelH = canvas.LineHeight(Font::Bold) + canvas.S(8);
    const int cx = p.CentreX();
    const int top = p.bottom - labelH - p.Height() / 5;
    if (look.beam) {
        const overlay::Rgb beam = ToOverlay(*look.beam);
        const int w = canvas.S(10);
        for (int b = 0; b < 6; ++b)
            canvas.FillAlpha(Box{cx - w / 2 - b * canvas.S(2), p.top + canvas.S(6), cx + w / 2 + b * canvas.S(2), top}, beam, static_cast<std::uint8_t>(60 - b * 9));
    }
    const Box label{cx - labelW / 2, top, cx + labelW / 2, top + labelH};
    canvas.FillAlpha(label, look.background ? ToOverlay(*look.background) : overlay::Rgb{0, 0, 0}, look.background ? 230 : 170);
    if (look.border) {
        canvas.Frame(label, ToOverlay(*look.border));
        canvas.Frame(label.Inflated(-1), ToOverlay(*look.border));
    }
    // The name in a colour that reads on the background.
    const edit::Rgb3 back = look.background.value_or(edit::Rgb3{0, 0, 0});
    const int light = back[0] * 299 + back[1] * 587 + back[2] * 114;
    canvas.Text(name, label, light > 140000 ? overlay::Rgb{20, 20, 20} : overlay::Rgb{199, 179, 119}, Font::Bold, kCenter | kVCenter | kSingleLine | kNoHighlight);
}

}  // namespace esrfe::ui
