#include "browse_view.hpp"

#include <algorithm>
#include <initializer_list>
#include <set>

#include "add_rule_view.hpp"
#include "help_view.hpp"
#include "item_rules_view.hpp"
#include "quick_rule_view.hpp"
#include "new_filter_view.hpp"
#include "version_name_view.hpp"
#include "rule_draft.h"
#include "rule_view.hpp"
#include "text_fold.hpp"
#include "ui_text.hpp"

namespace esrfe::ui {

using namespace overlay::text_flags;
using overlay::Font;

using overlay::UiFormat;
using overlay::UiText;

auto ActionColor(Action action, const overlay::Theme& theme) -> overlay::Rgb {
    switch (action) {
        case Action::Hide: return theme.danger;
        case Action::Show: return overlay::Rgb{96, 176, 92};
        case Action::Highlight: return theme.gold;
        case Action::Rename: return overlay::Rgb{112, 152, 224};
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

auto Loaded::Author() const -> std::string {
    const std::string& author = doc.GetSettings().author;
    return author.empty() ? UiText("the author") : author;
}

auto Loaded::Build(std::string text) -> bool {
    diags.clear();
    parsed = document::Document::Parse(std::move(text), doc, diags);
    stale = false;
    rules.clear();
    search.clear();
    off.clear();
    changed.clear();
    if (!parsed) return false;
    const auto& list = doc.Rules();
    rules.reserve(list.size());
    for (const document::Rule& rule : list) {
        RuleSummary summary = Describe(rule.value, context);
        // Only what the player sees in the list and on the card: a rule found for a word they cannot see looks wrong.
        std::string words = summary.actionLabel + "\n" + rule.id + "\n";
        for (const std::string& n : summary.whatAll) words += n + "\n";
        for (const std::string& c : summary.conditions) words += c + "\n";
        for (const std::string& e : summary.effects) words += e + "\n";
        if (summary.whatAll.empty()) words += summary.what + "\n";
        search.push_back(overlay::text::SearchTextUtf8(overlay::StripColorCodes(words)));
        rules.push_back(std::move(summary));
        const bool isOff = session != nullptr && session->IsOff(rule.id);
        const bool isChanged = session != nullptr && (session->Change(rule.id) != nullptr || rule.id.rfind('U', 0) == 0);
        off.push_back(isOff ? 1 : 0);
        changed.push_back(isChanged ? 1 : 0);
    }
    return true;
}

namespace {

// Reads and describes the filter. Runs on the overlay's UI thread (729 rules: a few milliseconds).
auto Load(const std::shared_ptr<Source>& sourcePtr, const FilterInfo& info, std::shared_ptr<const gamedata::GameData> game) -> std::shared_ptr<Loaded> {
    Source& source = *sourcePtr;
    auto loaded = std::make_shared<Loaded>();
    loaded->source = sourcePtr;
    loaded->info = info;
    loaded->game = std::move(game);
    loaded->language = source.Language();
    loaded->context.game = loaded->game.get();
    loaded->context.language = loaded->language;
    const Loaded* raw = loaded.get();
    loaded->context.style = [raw](std::string_view name) -> const document::Value* {
        for (const document::Style& s : raw->doc.Styles())
            if (s.name == name) return &s.value;
        return nullptr;
    };
    loaded->context.levelName = [raw](std::int64_t id) { return raw->LevelName(id); };

    // Through a session (the player's copy and changes); read only when that cannot be.
    std::string text;
    auto session = std::make_shared<edit::Session>();
    std::string why;
    if (session->Open(info.filterId, info.path, source.Folders(), why)) {
        loaded->session = session;
        text = session->ViewText();
    } else {
        loaded->sessionError = why;
        if (!source.ReadFile(info.path, text)) {
            loaded->error = UiFormat("The file could not be opened: {0}", {info.path});
            return loaded;
        }
    }
    if (!loaded->Build(std::move(text))) {
        loaded->error = loaded->diags.empty() ? UiText("The file is not a filter the loot filter can read.")
                                              : UiFormat("The file could not be read: {0}", {loaded->diags.front().message});
    }
    return loaded;
}

}  // namespace

BrowseView::BrowseView(std::shared_ptr<Source> source) : source_(std::move(source)) {
    search_.search = true;
    search_.placeholder = "Search the rules: an item, a stat, a word…";
    search_.onChange = [this](const std::string& text, Ui& ui) {
        highlight_ = overlay::MakeHighlight(text);
        Refilter();
        RebuildRows();
        ruleList_.SetCount(static_cast<int>(shown_.size()));
        ruleList_.Select(shown_.empty() ? -1 : 0, nullptr);
        ruleList_.SetScroll(0);
        ui.Repaint();
    };
    search_.onSubmit = [this](const std::string&, Ui& ui) {
        if (!shown_.empty()) ui.Focus(&ruleList_);
    };

    // The filter the game uses: choosing another switches the game to it.
    filterLabel_.text = "Filter";
    filterLabel_.font = Font::Small;
    filter_.translate = false;
    filter_.onChange = [this](int index, Ui& ui) { ChooseFilter(index, ui); };

    help_.label = "Help";
    help_.onClick = [](Ui& ui) { ui.PushView(std::make_shared<HelpView>()); };
    addRule_.label = "+ Add a rule";
    addRule_.style = overlay::Button::Style::Primary;
    addRule_.onClick = [this](Ui& ui) { StartRule(ui, nullptr); };
    editRule_.label = "Edit rule";
    editRule_.onClick = [this](Ui& ui) {
        const int row = ruleList_.Selected();
        if (!loaded_ || row < 0 || row >= static_cast<int>(shown_.size())) return;
        const int rule = shown_[static_cast<std::size_t>(row)];
        const std::string why = OpenRuleEditor(ui, loaded_, loaded_->doc.Rules()[static_cast<std::size_t>(rule)].id);
        if (!why.empty()) {
            overlay::DialogSpec spec;
            spec.title = "This rule cannot be edited here";
            spec.message = why;
            spec.confirm = "OK";
            spec.cancel.clear();
            spec.confirmIsDefault = true;
            ui.ShowDialog(spec, [](bool) {});
        }
    };

    // The filter's volume: saved in the player's version when the slider stops, heard at once.
    volumeLabel_.text = "Filter volume";
    volumeLabel_.font = Font::Small;
    volumeLabel_.flags = kRight | kVCenter | kSingleLine;
    volumeValue_.translate = false;
    volumeValue_.font = Font::Small;
    volumeValue_.flags = kLeft | kVCenter | kSingleLine;
    volume_.minimum = 0;
    volume_.maximum = 100;
    volume_.step = 5;
    volume_.onChange = [this](int v, Ui& ui) {
        volumeValue_.text = std::to_string(v);
        ui.Repaint();
    };
    volume_.onCommit = [this](int v, Ui& ui) { SaveVolume(v, ui); };
    volumeTest_.label = "Test";
    volumeTest_.onClick = [this](Ui&) { PlayTest(); };

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
    sectionList_.onClick = [this](int index, Ui& ui) {  // a click on a part opens or closes it
        if (RowIsPart(index)) {
            ToggleRow(index);
            ui.Repaint();
        }
    };
    sectionList_.onActivate = [this](int index, Ui& ui) {  // Enter / A: a part opens or closes, a section goes to its rules
        if (RowIsPart(index)) {
            ToggleRow(index);
            ui.Repaint();
        } else if (!shown_.empty()) {
            ui.Focus(&ruleList_);
        }
    };
    sectionList_.rowTooltip = [this](int index) -> std::string {
        if (!loaded_ || index < 0 || index >= static_cast<int>(rows_.size())) return {};
        const SectionRow& row = sections_[static_cast<std::size_t>(rows_[static_cast<std::size_t>(index)])];
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
                                         Apply(*answer, back, force);
                                         back.Repaint();
                                     });
    if (!queued) {
        FilterInfo none;
        Apply(none, ui, true);
        message_.text = UiText("The game did not answer. Close the editor and open it again.");
        ui.Repaint();
    }
}

void BrowseView::Apply(const FilterInfo& info, Ui& ui, bool force) {
    std::shared_ptr<const gamedata::GameData> game = source_->Game();
    // A save (a rule card's change) may have made or renamed a version: the list again at once, with the version the game
    // is switching to chosen (the game reads the new selection a moment later).
    const bool saved = loaded_ && loaded_->stale && loaded_->session != nullptr;
    const std::string active = saved && loaded_->session->IsCopy() ? loaded_->session->MergedId() : info.filterId;
    if (info.bridge && (force || saved || filters_.empty() || !loaded_ || loaded_->info.filterId != info.filterId)) RebuildFilters(active);
    if (loaded_ && loaded_->stale && loaded_->session != nullptr) {  // edited on a rule card: the list again
        loaded_->Build(loaded_->session->ViewText());
        Rebuild();
    }
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
    loaded_ = Load(source_, info, std::move(game));
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
    if (loaded_->session != nullptr && !loaded_->session->Conflicts().empty()) {
        AskConflicts(ui);
        return;
    }
    if (pendingRule_) StartRule(ui, pendingDraft_, pendingItem_);
}

void BrowseView::Rebuild() {
    if (loaded_->session != nullptr && !volume_.Dragging()) {
        volume_.SetValue(loaded_->session->SoundVolume());
        volumeValue_.text = std::to_string(volume_.value);
    }
    // The open parts and the chosen row stay open and chosen when the same filter is read again.
    std::vector<std::string> open;
    for (const SectionRow& row : sections_)
        if (row.part && row.expanded) open.push_back(row.name);
    const int chosenHeading = section_ >= 0 && section_ < static_cast<int>(sections_.size()) ? sections_[static_cast<std::size_t>(section_)].heading : -1;

    sections_.clear();
    const document::Document& doc = loaded_->doc;
    sections_.push_back(SectionRow{-1, false, doc.ActiveCount(), UiText("All rules")});
    const auto& headings = doc.Headings();
    int part = -1;
    section_ = 0;
    for (int h = 0; h < static_cast<int>(headings.size()); ++h) {
        const document::Heading& heading = headings[static_cast<std::size_t>(h)];
        SectionRow row{h, heading.level == document::Level::Part, heading.endRule - heading.firstRule, heading.name};
        if (row.part) {
            part = static_cast<int>(sections_.size());
            row.expanded = std::find(open.begin(), open.end(), row.name) != open.end();  // closed to start with
        } else {
            row.parent = heading.part >= 0 ? part : -1;
        }
        if (h == chosenHeading) section_ = static_cast<int>(sections_.size());
        sections_.push_back(std::move(row));
    }

    Refilter();
    RebuildRows();
    ruleList_.SetCount(static_cast<int>(shown_.size()));
    if (ruleList_.Selected() < 0 && !shown_.empty()) ruleList_.Select(0, nullptr);
}

void BrowseView::RebuildRows() {
    // With a search, every row counts its matches and rows without any are left out.
    const overlay::text::ParsedQuery query = overlay::text::ParseQuery(search_.Value());
    const bool searching = !query.empty();
    if (searching && loaded_) {
        std::vector<char> hit(static_cast<std::size_t>(loaded_->doc.ActiveCount()), 0);
        for (int r = 0; r < loaded_->doc.ActiveCount(); ++r)
            hit[static_cast<std::size_t>(r)] = overlay::text::Matches(query, loaded_->search[static_cast<std::size_t>(r)]) ? 1 : 0;
        for (SectionRow& row : sections_) {
            int first = 0, end = loaded_->doc.ActiveCount();
            if (row.heading >= 0) {
                const auto& h = loaded_->doc.Headings()[static_cast<std::size_t>(row.heading)];
                first = h.firstRule;
                end = h.endRule;
            }
            row.matches = 0;
            for (int r = first; r < end; ++r) row.matches += hit[static_cast<std::size_t>(r)];
        }
    }
    rows_.clear();
    for (int i = 0; i < static_cast<int>(sections_.size()); ++i) {
        const SectionRow& row = sections_[static_cast<std::size_t>(i)];
        if (searching && row.matches == 0 && i != section_) continue;
        if (row.parent >= 0 && !sections_[static_cast<std::size_t>(row.parent)].expanded && i != section_) continue;
        rows_.push_back(i);
    }
    sectionList_.SetCount(static_cast<int>(rows_.size()));
    const auto at = std::find(rows_.begin(), rows_.end(), section_);
    sectionList_.Select(at == rows_.end() ? -1 : static_cast<int>(at - rows_.begin()), nullptr);
}

auto BrowseView::RowName(int row) const -> std::string {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return {};
    return sections_[static_cast<std::size_t>(rows_[static_cast<std::size_t>(row)])].name;
}

auto BrowseView::RowIsPart(int row) const -> bool {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return false;
    return sections_[static_cast<std::size_t>(rows_[static_cast<std::size_t>(row)])].part;
}

auto BrowseView::RowExpanded(int row) const -> bool {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return false;
    return sections_[static_cast<std::size_t>(rows_[static_cast<std::size_t>(row)])].expanded;
}

void BrowseView::ToggleRow(int row) {
    if (!RowIsPart(row)) return;
    const int index = rows_[static_cast<std::size_t>(row)];
    SectionRow& part = sections_[static_cast<std::size_t>(index)];
    part.expanded = !part.expanded;
    // A section of a part that closes is no longer chosen: its part is.
    if (!part.expanded && section_ >= 0 && sections_[static_cast<std::size_t>(section_)].parent == index) {
        section_ = index;
        Refilter();
        ruleList_.SetCount(static_cast<int>(shown_.size()));
        ruleList_.Select(shown_.empty() ? -1 : 0, nullptr);
        ruleList_.SetScroll(0);
    }
    RebuildRows();
}

auto BrowseView::WhatShown(int shownIndex) const -> const std::string& {
    static const std::string none;
    if (shownIndex < 0 || shownIndex >= static_cast<int>(whatShown_.size())) return none;
    return whatShown_[static_cast<std::size_t>(shownIndex)];
}

auto BrowseView::ActiveOnLevel(int rule) const -> bool {
    // The level the game plays: rules off on it are drawn dim.
    const std::int64_t level = loaded_ ? loaded_->info.level : 0;
    if (level == 0) return true;
    const auto& levels = loaded_->rules[static_cast<std::size_t>(rule)].levels;
    return levels.empty() || std::find(levels.begin(), levels.end(), level) != levels.end();
}

void BrowseView::RebuildFilters(const std::string& activeId) {
    filters_ = edit::ListFilters(source_->Folders());
    filter_.options.clear();
    filter_.selected = -1;
    for (std::size_t i = 0; i < filters_.size(); ++i) {
        filter_.options.push_back(filters_[i].title);
        if (filters_[i].id == activeId) filter_.selected = static_cast<int>(i);
    }
    if (filter_.selected < 0 && !activeId.empty()) {  // in use, but not in the folders (a launcher path): listed as it is
        edit::FilterChoice c;
        c.id = activeId;
        c.title = activeId;
        filters_.insert(filters_.begin(), c);
        filter_.options.insert(filter_.options.begin(), activeId);
        filter_.selected = 0;
    }
    filter_.options.push_back(UiText("+ New filter…"));  // the new-filter questions
    filter_.visible = !source_->Folders().user.empty();
    filterLabel_.visible = filter_.visible;
    filterChosen_ = filter_.selected;
}

void BrowseView::ChooseFilter(int index, Ui& ui) {
    if (index == static_cast<int>(filters_.size())) {  // "+ New filter…"
        filter_.selected = filterChosen_;
        std::shared_ptr<bool> alive = alive_;
        ui.PushView(std::make_shared<NewFilterView>(source_, [this, alive](Ui& u, const std::string& id) {
            if (!*alive) return;
            switchError_.clear();
            switching_ = id;
            WaitForFilter(id, u.Now() + 4000, u);
        }));
        return;
    }
    if (index < 0 || index >= static_cast<int>(filters_.size())) return;
    filterChosen_ = index;
    const std::string id = filters_[static_cast<std::size_t>(index)].id;
    std::string error;
    if (!edit::SelectFilter(source_->Folders(), id, error)) {
        switchError_ = UiFormat("Not switched: {0}", {error});
        ui.Repaint();
        return;
    }
    switchError_.clear();
    switching_ = id;
    WaitForFilter(id, ui.Now() + 4000, ui);
}

void BrowseView::WaitForFilter(const std::string& id, std::uint64_t until, Ui& ui) {
    // The loot filter sees selection.json change and loads the filter a moment later: ask until it has.
    auto answer = std::make_shared<FilterInfo>();
    std::shared_ptr<Source> source = source_;
    std::shared_ptr<bool> alive = alive_;
    const bool queued = ui.RunOnGame([source, answer] { *answer = source->Filter(); },
                                     [this, alive, answer, id, until](Ui& back) {
                                         if (!*alive || switching_ != id) return;
                                         if (answer->filterId == id || back.Now() >= until) {
                                             if (answer->filterId != id) switchError_ = UiText("The loot filter has not loaded it yet.");
                                             switching_.clear();
                                             Apply(*answer, back, true);
                                             back.Repaint();
                                             return;
                                         }
                                         WaitForFilter(id, until, back);
                                     });
    if (!queued) switching_.clear();
}

void BrowseView::ChooseSection(int row) {
    if (row < 0 || row >= static_cast<int>(rows_.size())) return;
    section_ = rows_[static_cast<std::size_t>(row)];
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
    whatShown_.clear();
    for (int r = first; r < end; ++r) {
        if (!query.empty() && !overlay::text::Matches(query, loaded_->search[static_cast<std::size_t>(r)])) continue;
        shown_.push_back(r);
        const RuleSummary& s = loaded_->rules[static_cast<std::size_t>(r)];
        if (query.empty() || s.whatAll.size() <= 3) {
            whatShown_.push_back(s.what);
            continue;
        }
        // The items the search found go first, so the row shows why it matched ("Ber Rune, ... and 12 more").
        std::vector<std::string> found, rest;
        for (const std::string& name : s.whatAll) {
            const std::string folded = overlay::text::SearchTextUtf8(name);
            bool any = false;
            for (const overlay::text::QueryTerm& term : query.terms)
                if (!term.excluded && overlay::text::HasTerm(folded, term)) any = true;
            (any ? found : rest).push_back(name);
        }
        found.insert(found.end(), rest.begin(), rest.end());
        whatShown_.push_back(JoinNames(found, 3));
    }
}

namespace {

// A field the player changed, in words, with a value as the player would read it.
auto FieldWords(const std::string& key) -> std::string {
    if (key == "activeInLevels") return UiText("its filter levels");
    if (key == "hide" || key == "show") return UiText("what it does (hide or always show)");
    if (key == "itemCodes" || key == "itemTypes" || key == "uniqueOrSetIds") return UiText("its items");
    return UiFormat("its setting {0}", {key});
}

auto ValueWords(const Loaded& loaded, const std::string& key, const document::Value& value, bool absent) -> std::string {
    if (absent) return key == "activeInLevels" ? UiText("every level") : UiText("not set");
    if (key == "activeInLevels" && value.IsArray()) {
        std::vector<std::string> names;
        for (const document::Value& v : value.array) {
            std::int64_t id = 0;
            v.AsInteger(id);
            const std::string name = loaded.LevelName(id);
            names.push_back(name.empty() ? std::to_string(id) : name);
        }
        return JoinNames(names, names.size());
    }
    if (value.IsBool()) return value.boolean ? UiText("yes") : UiText("no");
    return document::Render(value);
}

}  // namespace

void BrowseView::AskConflicts(Ui& ui) {
    if (!loaded_ || loaded_->session == nullptr) return;
    std::shared_ptr<Loaded> loaded = loaded_;
    std::shared_ptr<bool> alive = alive_;
    const auto& conflicts = loaded->session->Conflicts();
    if (conflicts.empty()) {  // every one answered: the update is in
        loaded->session->FinishConflicts();
        std::string error;
        bool switched = false;
        if (!loaded->session->Save(error, switched)) message_.text = error;
        loaded->stale = true;
        Refresh(ui, true);
        return;
    }
    const edit::Conflict conflict = conflicts.front();
    const int index = loaded->doc.FindRule(conflict.id);
    const std::string about = index >= 0 ? overlay::StripColorCodes(loaded->rules[static_cast<std::size_t>(index)].what) : conflict.id;
    overlay::DialogSpec spec;
    spec.title = UiFormat("{0} updated the filter", {loaded->Author()});
    if (conflict.kind == edit::Conflict::Kind::Deleted) {
        spec.message = UiFormat("Rule {0} ({1}) is gone from {2}'s new version. You had changed it.", {conflict.id, about, loaded->Author()});
        spec.confirm = "Drop my change";
        spec.cancel = "Keep it as my rule";
    } else {
        spec.message = UiFormat("Rule {0} ({1}): you changed {2} to: {3}.\n{4}'s new version has: {5}.",
                                {conflict.id, about, FieldWords(conflict.key), ValueWords(*loaded, conflict.key, conflict.mine, conflict.mineRemoved),
                                 loaded->Author(), ValueWords(*loaded, conflict.key, conflict.now, conflict.nowAbsent)});
        spec.confirm = UiFormat("Use {0}'s", {loaded->Author()});
        spec.cancel = "Keep mine";
    }
    // Escape / B is the cancel button: keeping the player's version is the safe answer.
    spec.confirmIsDefault = false;
    ui.ShowDialog(spec, [this, alive, loaded, conflict, &ui](bool takeAuthors) {
        if (!*alive) return;
        loaded->session->Resolve(conflict, !takeAuthors);
        ui.PostToUi([this, alive](Ui& next) {
            if (*alive) AskConflicts(next);
        });
    });
}

void CommitChange(Ui& ui, const std::shared_ptr<Loaded>& loaded, const std::shared_ptr<bool>& alive,
                  std::function<void(edit::Session&)> change,
                  std::function<void(Ui&, bool, bool, const std::string&)> done, std::function<void(Ui&)> cancelled) {
    if (!loaded || loaded->session == nullptr) return;
    edit::Session& session = *loaded->session;
    const auto run = [loaded, alive, change, done](Ui& u) {
        if (!*alive) return;
        edit::Session& s = *loaded->session;
        change(s);
        std::string error;
        bool switched = false;
        const bool ok = s.Save(error, switched);
        loaded->stale = true;
        if (done) done(u, ok, switched, error);
        u.Repaint();
    };
    if (!session.NeedsName()) {
        run(ui);
        return;
    }
    // The first change to an author's filter: the player names the version it makes (or picks one they have).
    ui.PushView(std::make_shared<VersionNameView>(
        loaded, [alive, run](Ui& u) {
            if (*alive) run(u);
        },
        [alive, cancelled](Ui& u) {
            if (*alive && cancelled) cancelled(u);
        }));
}

void BrowseView::StartRule(Ui& ui, DraftMaker makeDraft, ItemRules item) {
    if (!loaded_ || !loaded_->parsed || loaded_->session == nullptr || loaded_->game == nullptr) {
        pendingRule_ = true;  // once the filter is read (Apply)
        pendingDraft_ = std::move(makeDraft);
        pendingItem_ = std::move(item);
        return;
    }
    pendingRule_ = false;
    pendingDraft_ = nullptr;
    pendingItem_ = ItemRules{};
    if (!item.name.empty() && makeDraft) {  // the edit-item key: what the player wants with this item
        ui.PushView(std::make_shared<QuickRuleView>(loaded_, std::move(item), std::move(makeDraft)));
        return;
    }
    RuleDraft draft = makeDraft ? makeDraft(*loaded_->game) : RuleDraft{};
    ui.PushView(std::make_shared<AddRuleView>(loaded_, std::move(draft), std::string{}));
}

void BrowseView::OpenRule(int shownIndex, Ui& ui) {
    if (!loaded_ || shownIndex < 0 || shownIndex >= static_cast<int>(shown_.size())) return;
    ui.PushView(std::make_shared<RuleView>(loaded_, shown_[static_cast<std::size_t>(shownIndex)]));
}

void BrowseView::SaveVolume(int volume, Ui& ui) {
    if (!loaded_ || loaded_->session == nullptr) return;
    std::shared_ptr<Loaded> loaded = loaded_;
    std::shared_ptr<bool> alive = alive_;
    const int before = loaded->session->SoundVolume();
    CommitChange(
        ui, loaded, alive_,
        [volume](edit::Session& s) {
            const document::Value v = document::Value::Integer(volume);
            s.SetSetting("soundVolume", &v);
        },
        [this, alive](Ui& u, bool ok, bool, const std::string& error) {
            if (!*alive) return;
            volumeError_ = ok ? std::string{} : UiFormat("Volume not saved: {0}", {error});
            if (ok) PlayTest();
            u.Repaint();
        },
        [this, alive, before](Ui& u) {  // no version of their own: the slider goes back
            if (!*alive) return;
            volume_.SetValue(before);
            volumeValue_.text = std::to_string(before);
            u.Repaint();
        });
}

void BrowseView::PlayTest() {
    if (!loaded_ || loaded_->session == nullptr) return;
    const std::vector<std::string> files = loaded_->session->SoundFiles();
    if (files.empty()) return;
    std::string file = files.front();
    for (const char* liked : {"tink.mp3", "tink.wav", "unique.mp3"})
        if (std::find(files.begin(), files.end(), liked) != files.end()) {
            file = liked;
            break;
        }
    source_->PlaySound(loaded_->session->SoundPath(file), source_->FileSoundVolume() * volume_.value / 100);
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
    const int settingsWidth = m.TextWidth(UiText(help_.label), Font::Body) + m.S(40);
    help_.box = Box{area.right - settingsWidth, area.top, area.right, area.top + barHeight};
    const int addWidth = m.TextWidth(UiText(addRule_.label), Font::Body) + m.S(28);
    addRule_.box = Box{help_.box.left - gap - addWidth, area.top, help_.box.left - gap, area.top + barHeight};
    addRule_.visible = !empty_ && loaded_ && loaded_->session != nullptr && loaded_->game != nullptr;
    const int editWidth = m.TextWidth(UiText(editRule_.label), Font::Body) + m.S(28);
    editRule_.box = Box{addRule_.box.left - gap - editWidth, area.top, addRule_.box.left - gap, area.top + barHeight};
    editRule_.visible = addRule_.visible;
    editRule_.enabled = ruleList_.Selected() >= 0 && ruleList_.Selected() < static_cast<int>(shown_.size());
    int right = (editRule_.visible ? editRule_.box.left : addRule_.visible ? addRule_.box.left : help_.box.left) - gap;
    if (filter_.visible) {
        int widest = m.S(160);
        for (const std::string& option : filter_.options) widest = std::max(widest, m.TextWidth(option, Font::Body) + m.S(40));
        widest = std::min(widest, area.Width() / 3);
        filter_.box = Box{right - widest, area.top, right, area.top + barHeight};
        const int labelWidth = m.TextWidth(UiText(filterLabel_.text), Font::Small) + m.S(8);
        filterLabel_.box = Box{filter_.box.left - labelWidth, area.top, filter_.box.left - tight, area.top + barHeight};
        right = filterLabel_.box.left - gap;
    }
    search_.box = Box{area.left, area.top, right, area.top + barHeight};
    search_.enabled = !empty_;

    // A line about the filter.
    const int infoTop = search_.box.bottom + tight;
    const int infoHeight = std::max(m.S(26), m.LineHeight(Font::Small) + m.S(10));
    const bool volume = !empty_ && loaded_ && loaded_->session != nullptr && !loaded_->session->SoundFiles().empty();
    for (Widget* w : std::initializer_list<Widget*>{&volumeLabel_, &volume_, &volumeValue_, &volumeTest_}) w->visible = volume;
    int infoRight = area.right;
    if (volume) {
        const int testWidth = m.TextWidth(UiText(volumeTest_.label), Font::Body) + m.S(24);
        volumeTest_.box = Box{area.right - testWidth, infoTop, area.right, infoTop + infoHeight};
        const int valueWidth = m.TextWidth("100", Font::Small) + m.S(8);
        volumeValue_.box = Box{volumeTest_.box.left - gap - valueWidth, infoTop, volumeTest_.box.left - gap, infoTop + infoHeight};
        volume_.box = Box{volumeValue_.box.left - m.S(160), infoTop, volumeValue_.box.left - tight, infoTop + infoHeight};
        const int labelWidth = m.TextWidth(UiText(volumeLabel_.text), Font::Small) + m.S(8);
        volumeLabel_.box = Box{volume_.box.left - labelWidth, infoTop, volume_.box.left - tight, infoTop + infoHeight};
        volumeValue_.color = theme.text;
        infoRight = volumeLabel_.box.left - gap;
    }
    info_.box = Box{area.left, infoTop, infoRight, infoTop + infoHeight};
    const std::string& problem = !switchError_.empty() ? switchError_ : volumeError_;
    info_.color = problem.empty() ? theme.dim : theme.danger;
    if (loaded_ && loaded_->parsed) {
        const document::Settings& s = loaded_->doc.GetSettings();
        std::string text = UiFormat("{0} rules", {std::to_string(loaded_->doc.ActiveCount())});
        if (!s.version.empty()) text += "  ·  " + UiFormat("version {0}", {s.version});
        if (!s.author.empty()) text += "  ·  " + UiFormat("by {0}", {s.author});
        for (const document::FilterLevel& l : s.filterLevels)
            if (l.id == loaded_->info.level) text += "  ·  " + UiFormat("level: {0}", {PickLanguage(l.name, loaded_->language)});
        if (loaded_->session != nullptr && loaded_->session->IsCopy()) text += "  ·  " + UiText("your version: your changes on top of the author's filter");
        else if (loaded_->info.shipped) text += "  ·  " + UiText("ships with the mod");
        if (loaded_->session == nullptr && !loaded_->sessionError.empty()) text += "  ·  " + UiFormat("read only: {0}", {loaded_->sessionError});
        if (!problem.empty()) text = problem;
        info_.text = text;
    } else {
        info_.text = !switching_.empty() ? UiText("Switching the filter…") : problem;
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
    sectionList_.SetCount(static_cast<int>(rows_.size()));

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
    out = {&search_, &filterLabel_, &filter_, &editRule_, &addRule_, &help_, &info_, &volumeLabel_, &volume_, &volumeValue_, &volumeTest_,
           &sectionList_, &count_, &ruleList_, &message_};
}

void BrowseView::Paint(Canvas&) {}

void BrowseView::PaintSectionRow(Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState&) {
    if (index < 0 || index >= static_cast<int>(rows_.size())) return;
    const SectionRow& s = sections_[static_cast<std::size_t>(rows_[static_cast<std::size_t>(index)])];
    const overlay::Theme& theme = canvas.GetTheme();
    const bool top = s.heading < 0 || s.part;
    const int indent = s.part ? canvas.S(26) : top ? canvas.S(10) : canvas.S(34);
    if (s.part) {  // an arrow: right when closed, down when open
        const int size = canvas.S(8);
        const int x = row.left + canvas.S(10);
        const int y = row.CentreY();
        for (int i = 0; i < size; ++i) {
            if (s.expanded) {  // down: rows get narrower
                const int half = (size - i) / 2;
                canvas.Fill(Box{x + size / 2 - half, y - size / 4 + i / 2, x + size / 2 + half, y - size / 4 + i / 2 + 1}, theme.gold);
            } else {           // right: columns get shorter
                const int half = (size - i) / 2;
                canvas.Fill(Box{x + i / 2, y - half, x + i / 2 + 1, y + half}, theme.gold);
            }
        }
    }
    const bool searching = !search_.Value().empty();
    const std::string count = searching ? std::to_string(s.matches) : std::to_string(s.count);
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
    const bool isOff = loaded_->off[static_cast<std::size_t>(r)] != 0;
    const bool isChanged = loaded_->changed[static_cast<std::size_t>(r)] != 0;
    const bool active = ActiveOnLevel(r) && !isOff;

    // The action badge.
    const int pad = canvas.S(10);
    const int badgeWidth = canvas.S(104);
    const int badgeHeight = canvas.LineHeight(Font::Small) + canvas.S(6);
    const Box badge{row.left + pad, row.CentreY() - badgeHeight / 2, row.left + pad + badgeWidth, row.CentreY() - badgeHeight / 2 + badgeHeight};
    const overlay::Rgb color = active ? ActionColor(s.action, theme) : theme.dim;
    canvas.FillAlpha(badge, color, active ? 64 : 32);
    canvas.Frame(badge, color);
    canvas.Text(isOff ? UiText("Off") : s.actionLabel, badge, active ? theme.text : theme.dim, Font::Small, kCenter | kVCenter | kSingleLine | kNoHighlight);

    // The id at the right, the items and the conditions.
    const std::string mark = isChanged ? (rule.id.rfind('U', 0) == 0 ? UiText("yours") : UiText("changed")) : std::string{};
    const int markWidth = mark.empty() ? 0 : canvas.TextWidth(mark, Font::Small) + pad;
    const int idWidth = canvas.TextWidth(rule.id, Font::Small) + pad + markWidth;
    const int left = badge.right + pad;
    const int split = row.top + canvas.S(5) + canvas.LineHeight(Font::Bold);
    canvas.SetHighlight(&highlight_);
    const Box what{left, row.top + canvas.S(4), row.right - idWidth - pad, split};
    canvas.PushClip(what);
    canvas.Text(WhatShown(index), what, active ? theme.text : theme.dim, Font::Bold, kSingleLine);
    canvas.PopClip();
    std::string line;
    if (isOff) line = UiText("Switched off") + "  ·  ";
    else if (!active) line = UiText("Not on this level") + "  ·  ";
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
    if (!mark.empty())
        canvas.Text(mark, Box{row.right - idWidth - pad, row.top + canvas.S(4), row.right - pad - (idWidth - markWidth), split}, theme.gold,
                    Font::Small, kRight | kSingleLine | kNoHighlight);
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
        const int rows = static_cast<int>(rows_.size());
        if (rows == 0) return true;
        const int row = std::max(0, sectionList_.Selected());
        ChooseSection((row + step + rows) % rows);
        ui.Repaint();
        return true;
    }
    return false;
}

auto BrowseView::InitialFocus() -> Widget* { return empty_ ? static_cast<Widget*>(&help_) : &ruleList_; }

}  // namespace esrfe::ui
