#include "browse_view.hpp"

#include <algorithm>

#include "rule_view.hpp"
#include "text_fold.hpp"
#include "ui_text.hpp"

namespace esrfe::ui {

using namespace overlay::text_flags;
using overlay::Font;
using overlay::Rgb;
using overlay::UiFormat;
using overlay::UiText;

auto ActionColor(Action action, const overlay::Theme& theme) -> Rgb {
    switch (action) {
        case Action::Hide: return theme.danger;
        case Action::Show: return Rgb{96, 176, 92};
        case Action::Highlight: return theme.gold;
        case Action::Rename: return Rgb{112, 152, 224};
        case Action::Other: break;
    }
    return theme.dim;
}

auto Loaded::LevelName(std::int64_t id) const -> std::string {
    for (const document::FilterLevel& l : doc.GetSettings().filterLevels)
        if (l.id == id) return PickLanguage(l.name, language);
    for (const LevelInfo& l : info.levels)
        if (l.id == id) return l.name;
    return {};
}

auto Loaded::Place(int rule) const -> std::string {
    if (rule < 0 || rule >= static_cast<int>(doc.Rules().size())) return {};
    const document::Rule& r = doc.Rules()[static_cast<std::size_t>(rule)];
    const auto& headings = doc.Headings();
    std::string out;
    if (r.part >= 0 && r.part < static_cast<int>(headings.size())) out = headings[static_cast<std::size_t>(r.part)].name;
    if (r.section >= 0 && r.section < static_cast<int>(headings.size()))
        out += (out.empty() ? "" : " > ") + headings[static_cast<std::size_t>(r.section)].name;
    return out;
}

namespace {

// Reads and describes the filter. Runs on the overlay's UI thread (729 rules: a few milliseconds).
auto Load(Source& source, const FilterInfo& info, std::shared_ptr<const gamedata::GameData> game) -> std::shared_ptr<Loaded> {
    auto loaded = std::make_shared<Loaded>();
    loaded->info = info;
    loaded->game = std::move(game);
    loaded->language = source.Language();
    std::string text;
    if (!source.ReadFile(info.path, text)) {
        loaded->error = UiFormat("The file could not be opened: {0}", {info.path});
        return loaded;
    }
    loaded->parsed = document::Document::Parse(std::move(text), loaded->doc, loaded->diags);
    if (!loaded->parsed) {
        loaded->error = loaded->diags.empty() ? UiText("The file is not a filter the loot filter can read.")
                                              : UiFormat("The file could not be read: {0}", {loaded->diags.front().message});
        return loaded;
    }
    DescribeContext context;
    context.game = loaded->game.get();
    context.language = loaded->language;
    const Loaded* raw = loaded.get();
    context.style = [raw](std::string_view name) -> const document::Value* {
        for (const document::Style& s : raw->doc.Styles())
            if (s.name == name) return &s.value;
        return nullptr;
    };
    context.levelName = [raw](std::int64_t id) { return raw->LevelName(id); };
    const auto& rules = loaded->doc.Rules();
    loaded->rules.reserve(rules.size());
    loaded->search.reserve(rules.size());
    for (const document::Rule& rule : rules) {
        RuleSummary summary = Describe(rule.value, context);
        loaded->search.push_back(overlay::text::SearchTextUtf8(
            overlay::StripColorCodes(summary.search + rule.id + "\n" + rule.title + "\n" + loaded->Place(static_cast<int>(loaded->rules.size())))));
        loaded->rules.push_back(std::move(summary));
    }
    return loaded;
}

}  // namespace

BrowseView::BrowseView(std::shared_ptr<Source> source) : source_(std::move(source)) {
    search_.search = true;
    search_.placeholder = "Search the rules: an item, a stat, a word…";
    search_.tooltip = "Words must all be found; \"quoted words\" together; -word leaves rules out.";
    search_.onChange = [this](const std::string& text, Ui& ui) {
        highlight_ = overlay::MakeHighlight(text);
        Refilter();
        ruleList_.SetCount(static_cast<int>(shown_.size()));
        ruleList_.Select(shown_.empty() ? -1 : 0, nullptr);
        ruleList_.SetScroll(0);
        ui.Repaint();
    };
    search_.onSubmit = [this](const std::string&, Ui& ui) {
        if (!shown_.empty()) ui.Focus(&ruleList_);
    };

    levelLabel_.text = "Level";
    levelLabel_.font = Font::Small;
    level_.translate = false;
    level_.tooltip = "The rules on this filter level. Rules for other levels are greyed out. "
                     "The game's loot filter settings choose the level the game uses.";
    level_.onChange = [this](int, Ui& ui) {
        Refilter();
        ruleList_.SetCount(static_cast<int>(shown_.size()));
        ui.Repaint();
    };

    gameSettings_.label = "Game loot filter settings";
    gameSettings_.tooltip = "The game's own loot filter screen: choose the filter and its level, Show Hidden Items.";
    gameSettings_.onClick = [this](Ui&) { source_->OpenGameSettings(); };

    info_.translate = false;
    info_.font = Font::Small;
    info_.useThemeColor = false;
    info_.flags = kVCenter | kSingleLine;

    sectionList_.emptyText = "No sections";
    sectionList_.paintRow = [this](Canvas& c, int i, const Box& row, const overlay::ListView::RowState& s) { PaintSectionRow(c, i, row, s); };
    sectionList_.onSelect = [this](int index, Ui& ui) {
        ChooseSection(index);
        ui.Repaint();
    };
    sectionList_.onActivate = [this](int, Ui& ui) {
        if (!shown_.empty()) ui.Focus(&ruleList_);
    };
    sectionList_.rowTooltip = [this](int index) -> std::string {
        if (!loaded_ || index < 0 || index >= static_cast<int>(sections_.size())) return {};
        const SectionRow& row = sections_[static_cast<std::size_t>(index)];
        if (row.heading < 0) return {};
        const auto& h = loaded_->doc.Headings()[static_cast<std::size_t>(row.heading)];
        return h.description.empty() ? std::string{} : h.description;
    };

    count_.translate = false;
    count_.font = Font::Small;
    count_.useThemeColor = false;

    ruleList_.emptyText = "No rules match";
    ruleList_.paintRow = [this](Canvas& c, int i, const Box& row, const overlay::ListView::RowState& s) { PaintRuleRow(c, i, row, s); };
    ruleList_.onActivate = [this](int index, Ui& ui) { OpenRule(index, ui); };
    ruleList_.rowTooltip = [this](int index) -> std::string {
        if (!loaded_ || index < 0 || index >= static_cast<int>(shown_.size())) return {};
        const int r = shown_[static_cast<std::size_t>(index)];
        const RuleSummary& s = loaded_->rules[static_cast<std::size_t>(r)];
        std::string tip = s.actionLabel + ": " + overlay::StripColorCodes(s.what);
        for (const std::string& c : s.conditions) tip += "\n  " + c;
        for (const std::string& e : s.effects) tip += "\n  " + overlay::StripColorCodes(e);
        const std::string& title = loaded_->doc.Rules()[static_cast<std::size_t>(r)].title;
        if (!title.empty()) tip += "\n\n" + title;
        if (!ActiveOnLevel(r)) tip += "\n\n" + UiText("Not used on this filter level.");
        return tip;
    };

    message_.translate = false;
    message_.flags = kCenter | kVCenter;
}

auto BrowseView::Title() const -> std::string {
    if (loaded_ && loaded_->parsed) {
        const std::string& title = loaded_->doc.GetSettings().title;
        if (!title.empty()) return UiFormat("Loot Filter Editor - {0}", {title});
    }
    return UiText("Loot Filter Editor");
}

void BrowseView::Shown(Ui& ui) { Refresh(ui); }

void BrowseView::Refresh(Ui& ui, bool force) {
    auto answer = std::make_shared<FilterInfo>();
    std::shared_ptr<Source> source = source_;
    std::shared_ptr<bool> alive = alive_;
    const bool queued = ui.RunOnGame([source, answer] { *answer = source->Filter(); },
                                     [this, alive, answer, force](Ui& back) {
                                         if (!*alive) return;
                                         Apply(*answer, force);
                                         back.Repaint();
                                     });
    if (!queued) {
        FilterInfo none;
        Apply(none, true);
        message_.text = UiText("The game did not answer. Close the editor and open it again.");
        ui.Repaint();
    }
}

void BrowseView::Apply(const FilterInfo& info, bool force) {
    std::shared_ptr<const gamedata::GameData> game = source_->Game();
    const bool sameFile = loaded_ && loaded_->info.path == info.path && loaded_->info.textHash == info.textHash &&
                          info.textHash != 0 && loaded_->info.loaded == info.loaded;
    const bool gameArrived = gameWasMissing_ && game != nullptr;
    gameWasMissing_ = game == nullptr;
    if (!force && sameFile && !gameArrived) {
        if (loaded_->info.level != info.level) {  // the game's level changed: follow it
            loaded_->info = info;
            Rebuild();
        }
        return;
    }
    if (!info.bridge || !info.loaded || game == nullptr) {
        loaded_.reset();
        empty_ = true;
        if (!info.bridge) {
            message_.text = UiText("The loot filter plugin did not answer.\nIs it installed and up to date?");
        } else if (!info.loaded) {
            message_.text = UiText("No loot filter is loaded.\nChoose one in the game's loot filter settings.");
            if (!info.failure.empty()) message_.text += "\n\n" + info.failure;
        } else {
            const std::string problem = source_->GameDataProblem();
            message_.text = problem.empty() ? UiText("Reading the mod's item data…") : UiFormat("The mod's item data could not be read: {0}", {problem});
        }
        sections_.clear();
        shown_.clear();
        sectionList_.SetCount(0);
        ruleList_.SetCount(0);
        return;
    }
    loaded_ = Load(*source_, info, std::move(game));
    if (!loaded_->parsed) {
        empty_ = true;
        message_.text = loaded_->error;
        sections_.clear();
        shown_.clear();
        sectionList_.SetCount(0);
        ruleList_.SetCount(0);
        return;
    }
    empty_ = false;
    Rebuild();
}

void BrowseView::Rebuild() {
    sections_.clear();
    const document::Document& doc = loaded_->doc;
    sections_.push_back(SectionRow{-1, false, doc.ActiveCount(), UiText("All rules")});
    const auto& headings = doc.Headings();
    for (int h = 0; h < static_cast<int>(headings.size()); ++h) {
        const document::Heading& heading = headings[static_cast<std::size_t>(h)];
        sections_.push_back(SectionRow{h, heading.level == document::Level::Part, heading.endRule - heading.firstRule, heading.name});
    }
    if (section_ >= static_cast<int>(sections_.size())) section_ = 0;
    sectionList_.SetCount(static_cast<int>(sections_.size()));
    sectionList_.Select(section_, nullptr);

    // The level choices: every level, then the filter's own; the game's level chosen to start with.
    const std::int64_t chosen = level_.selected > 0 && !loaded_->info.levels.empty() ? ChosenLevel() : loaded_->info.level;
    level_.options = {UiText("Every level")};
    const auto& levels = doc.GetSettings().filterLevels;
    level_.selected = 0;
    for (const document::FilterLevel& l : levels) {
        std::string name = PickLanguage(l.name, loaded_->language);
        if (l.id == loaded_->info.level) name = UiFormat("{0} (in use)", {name});
        level_.options.push_back(name);
        if (l.id == chosen) level_.selected = static_cast<int>(level_.options.size()) - 1;
    }
    level_.visible = !levels.empty();
    levelLabel_.visible = level_.visible;

    Refilter();
    ruleList_.SetCount(static_cast<int>(shown_.size()));
    if (ruleList_.Selected() < 0 && !shown_.empty()) ruleList_.Select(0, nullptr);
}

auto BrowseView::ChosenLevel() const -> std::int64_t {
    if (!loaded_ || level_.selected <= 0) return 0;
    const auto& levels = loaded_->doc.GetSettings().filterLevels;
    const std::size_t i = static_cast<std::size_t>(level_.selected - 1);
    return i < levels.size() ? levels[i].id : 0;
}

auto BrowseView::ActiveOnLevel(int rule) const -> bool {
    const std::int64_t level = ChosenLevel();
    if (level == 0 || !loaded_) return true;
    const auto& levels = loaded_->rules[static_cast<std::size_t>(rule)].levels;
    return levels.empty() || std::find(levels.begin(), levels.end(), level) != levels.end();
}

void BrowseView::ChooseSection(int row) {
    if (row < 0 || row >= static_cast<int>(sections_.size())) return;
    section_ = row;
    sectionList_.Select(row, nullptr);
    Refilter();
    ruleList_.SetCount(static_cast<int>(shown_.size()));
    ruleList_.Select(shown_.empty() ? -1 : 0, nullptr);
    ruleList_.SetScroll(0);
}

void BrowseView::Refilter() {
    shown_.clear();
    if (!loaded_ || !loaded_->parsed) return;
    int first = 0;
    int end = loaded_->doc.ActiveCount();
    if (section_ > 0 && section_ < static_cast<int>(sections_.size())) {
        const auto& h = loaded_->doc.Headings()[static_cast<std::size_t>(sections_[static_cast<std::size_t>(section_)].heading)];
        first = h.firstRule;
        end = h.endRule;
    }
    const overlay::text::ParsedQuery query = overlay::text::ParseQuery(search_.Value());
    for (int r = first; r < end; ++r) {
        if (query.empty() || overlay::text::Matches(query, loaded_->search[static_cast<std::size_t>(r)])) shown_.push_back(r);
    }
}

void BrowseView::OpenRule(int shownIndex, Ui& ui) {
    if (!loaded_ || shownIndex < 0 || shownIndex >= static_cast<int>(shown_.size())) return;
    ui.PushView(std::make_shared<RuleView>(loaded_, shown_[static_cast<std::size_t>(shownIndex)]));
}

void BrowseView::Layout(const LayoutContext& c) {
    overlay::Measurer& m = c.measure;
    const Box& area = c.content;
    const overlay::Theme& theme = m.GetTheme();

    const int gap = m.S(8);
    const int tight = m.S(4);
    const int body = m.LineHeight(Font::Body);
    const int barHeight = std::max(m.S(30), body + m.S(10));

    // The top bar: search, level, the game's settings.
    const int settingsWidth = m.TextWidth(UiText(gameSettings_.label), Font::Body) + m.S(28);
    gameSettings_.box = Box{area.right - settingsWidth, area.top, area.right, area.top + barHeight};
    int right = gameSettings_.box.left - gap;
    if (level_.visible) {
        int widest = m.S(120);
        for (const std::string& option : level_.options) widest = std::max(widest, m.TextWidth(option, Font::Body) + m.S(40));
        widest = std::min(widest, area.Width() / 3);
        level_.box = Box{right - widest, area.top, right, area.top + barHeight};
        const int labelWidth = m.TextWidth(UiText(levelLabel_.text), Font::Small) + m.S(8);
        levelLabel_.box = Box{level_.box.left - labelWidth, area.top, level_.box.left - tight, area.top + barHeight};
        right = levelLabel_.box.left - gap;
    }
    search_.box = Box{area.left, area.top, right, area.top + barHeight};
    search_.enabled = !empty_;

    // A line about the filter.
    const int infoTop = search_.box.bottom + tight;
    info_.box = Box{area.left, infoTop, area.right, infoTop + m.S(20)};
    info_.color = theme.dim;
    if (loaded_ && loaded_->parsed) {
        const document::Settings& s = loaded_->doc.GetSettings();
        std::string text = UiFormat("{0} rules", {std::to_string(loaded_->doc.ActiveCount())});
        if (!s.version.empty()) text += "  ·  " + UiFormat("version {0}", {s.version});
        if (!s.author.empty()) text += "  ·  " + UiFormat("by {0}", {s.author});
        if (loaded_->info.shipped) text += "  ·  " + UiText("ships with the mod");
        info_.text = text;
    } else {
        info_.text.clear();
    }

    const int listsTop = info_.box.bottom + tight;
    message_.box = Box{area.left, listsTop, area.right, area.bottom};
    message_.color = theme.text;
    message_.visible = empty_;
    sectionList_.visible = !empty_;
    ruleList_.visible = !empty_;
    count_.visible = !empty_;

    const int leftWidth = std::clamp(area.Width() * 27 / 100, m.S(200), m.S(320));
    sectionList_.box = Box{area.left, listsTop, area.left + leftWidth, area.bottom};
    sectionList_.rowHeight = std::max(m.S(24), m.LineHeight(Font::Bold) + m.S(8));
    sectionList_.rowGap = m.S(2);
    sectionList_.SetCount(static_cast<int>(sections_.size()));

    const int rulesLeft = sectionList_.box.right + gap;
    count_.box = Box{rulesLeft, listsTop, area.right, listsTop + m.S(20)};
    count_.color = theme.dim;
    if (loaded_ && section_ >= 0 && section_ < static_cast<int>(sections_.size())) {
        const SectionRow& row = sections_[static_cast<std::size_t>(section_)];
        count_.text = search_.Value().empty()
                          ? UiFormat("{0}: {1} rules", {row.name, std::to_string(shown_.size())})
                          : UiFormat("{0}: {1} of {2} rules match", {row.name, std::to_string(shown_.size()), std::to_string(row.count)});
    }
    ruleList_.box = Box{rulesLeft, count_.box.bottom + tight, area.right, area.bottom};
    ruleList_.rowHeight = m.LineHeight(Font::Bold) + m.LineHeight(Font::Small) + m.S(12);
    ruleList_.rowGap = tight;
    ruleList_.SetCount(static_cast<int>(shown_.size()));
}

void BrowseView::FocusList(std::vector<Widget*>& out) {
    out = {&search_, &levelLabel_, &level_, &gameSettings_, &info_, &sectionList_, &count_, &ruleList_, &message_};
}

void BrowseView::Paint(Canvas&) {}

void BrowseView::PaintSectionRow(Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
    if (index < 0 || index >= static_cast<int>(sections_.size())) return;
    const SectionRow& s = sections_[static_cast<std::size_t>(index)];
    const overlay::Theme& theme = canvas.GetTheme();
    const bool top = s.heading < 0 || s.part;
    const int indent = top ? canvas.S(10) : canvas.S(24);
    const std::string count = std::to_string(s.count);
    const int countWidth = canvas.TextWidth(count, Font::Small) + canvas.S(10);
    const Box text{row.left + indent, row.top, row.right - countWidth - canvas.S(4), row.bottom};
    canvas.PushClip(text);
    canvas.Text(s.name, text, top ? theme.headerGold : theme.text, top ? Font::Bold : Font::Body, kVCenter | kSingleLine);
    canvas.PopClip();
    canvas.Text(count, Box{row.right - countWidth, row.top, row.right - canvas.S(8), row.bottom}, theme.dim, Font::Small,
                kRight | kVCenter | kSingleLine);
}

void BrowseView::PaintRuleRow(Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
    if (!loaded_ || index < 0 || index >= static_cast<int>(shown_.size())) return;
    const int r = shown_[static_cast<std::size_t>(index)];
    const RuleSummary& s = loaded_->rules[static_cast<std::size_t>(r)];
    const document::Rule& rule = loaded_->doc.Rules()[static_cast<std::size_t>(r)];
    const overlay::Theme& theme = canvas.GetTheme();
    const bool active = ActiveOnLevel(r);

    // The action badge.
    const int pad = canvas.S(10);
    const int badgeWidth = canvas.S(104);
    const int badgeHeight = canvas.LineHeight(Font::Small) + canvas.S(6);
    const Box badge{row.left + pad, row.CentreY() - badgeHeight / 2, row.left + pad + badgeWidth, row.CentreY() - badgeHeight / 2 + badgeHeight};
    const Rgb color = active ? ActionColor(s.action, theme) : theme.dim;
    canvas.FillAlpha(badge, color, active ? 64 : 32);
    canvas.Frame(badge, color);
    canvas.Text(s.actionLabel, badge, active ? theme.text : theme.dim, Font::Small, kCenter | kVCenter | kSingleLine | kNoHighlight);

    // The id at the right, the items and the conditions.
    const int idWidth = canvas.TextWidth(rule.id, Font::Small) + pad;
    const int left = badge.right + pad;
    const int split = row.top + canvas.S(5) + canvas.LineHeight(Font::Bold);
    canvas.SetHighlight(&highlight_);
    const Box what{left, row.top + canvas.S(4), row.right - idWidth - pad, split};
    canvas.PushClip(what);
    canvas.Text(s.what, what, active ? theme.text : theme.dim, Font::Bold, kSingleLine);
    canvas.PopClip();
    std::string line;
    if (!active) line = UiText("Not on this level") + "  ·  ";
    for (const std::string& c : s.conditions) line += (line.empty() || line.ends_with("·  ") ? "" : "  ·  ") + c;
    if (s.conditions.empty() && !s.effects.empty()) {
        for (const std::string& e : s.effects) line += (line.empty() || line.ends_with("·  ") ? "" : "  ·  ") + overlay::StripColorCodes(e);
    }
    if (line.empty()) line = UiText("Every time");
    const Box when{left, split + canvas.S(2), row.right - pad, row.bottom - canvas.S(2)};
    canvas.PushClip(when);
    canvas.Text(line, when, theme.dim, Font::Small, kSingleLine);
    canvas.PopClip();
    canvas.SetHighlight(nullptr);
    canvas.Text(rule.id, Box{row.right - idWidth - pad, row.top + canvas.S(4), row.right - pad, split}, theme.dim, Font::Small,
                kRight | kSingleLine | kNoHighlight);
}

void BrowseView::HelpRows(const Widget* focused, std::vector<overlay::HelpRow>& out) const {
    if (focused == &ruleList_) out.push_back({overlay::Glyph::A, "Enter", "Open the rule"});
    out.push_back({overlay::Glyph::LBRB, "", "Previous / next section"});
    out.push_back({overlay::Glyph::None, "Ctrl+F", "Search"});
}

auto BrowseView::Key(const overlay::KeyEvent& event, Ui& ui) -> bool {
    if (event.key == overlay::Key::Other && event.mods.ctrl && event.vk == 'F' && search_.enabled) {
        ui.Focus(&search_);
        search_.edit.SelectAll();
        ui.Repaint();
        return true;
    }
    return false;
}

auto BrowseView::Pad(const overlay::PadEvent& event, Ui& ui) -> bool {
    if (empty_) return false;
    if (event.button == overlay::pad::Button::LB || event.button == overlay::pad::Button::RB) {
        const int step = event.button == overlay::pad::Button::LB ? -1 : 1;
        const int rows = static_cast<int>(sections_.size());
        if (rows == 0) return true;
        ChooseSection((section_ + step + rows) % rows);
        ui.Repaint();
        return true;
    }
    return false;
}

auto BrowseView::InitialFocus() -> Widget* { return empty_ ? static_cast<Widget*>(&gameSettings_) : &ruleList_; }

}  // namespace esrfe::ui
