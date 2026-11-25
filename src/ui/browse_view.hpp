// The editor's first screen: the loaded filter, its parts and sections on the left, its rules on the right as plain
// sentences ("Hide · Flawless Amethyst and 13 more · Outside towns"), a search box over both, the filter level to
// look at (the game's own to start with), and a button to the game's own loot filter settings. A rule opens its card
// (RuleView). Read-only until the editing milestone.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "color_text.hpp"
#include "document.h"
#include "pickers.hpp"
#include "rule_text.h"
#include "source.h"
#include "view.hpp"
#include "widgets.hpp"

namespace esrfe::ui {

using overlay::Box;
using overlay::Canvas;
using overlay::LayoutContext;
using overlay::Ui;
using overlay::Widget;

// The badge colour of an action.
auto ActionColor(Action action, const overlay::Theme& theme) -> overlay::Rgb;

// What the browse view and the rule cards share: the filter as the player has it (the author's with the player's
// changes, rules that are off still in it), each rule in words, and the session that edits it.
struct Loaded {
    std::shared_ptr<const gamedata::GameData> game;
    gamedata::Language language = gamedata::Language::enUS;
    FilterInfo info;
    std::shared_ptr<edit::Session> session;  // nullptr: read only (sessionError says why)
    std::shared_ptr<Source> source;
    std::string sessionError;
    document::Document doc;
    std::vector<RuleSummary> rules;   // one per doc.Rules()
    std::vector<std::string> search;  // the search form of each rule's words
    std::vector<char> off;            // per rule: switched off by the player
    std::vector<char> changed;        // per rule: the player changed it (or it is the player's own)
    std::vector<document::Diag> diags;
    bool parsed = false;
    bool stale = false;               // edited since the list was built: Build again before showing it
    std::string error;                // why the file could not be read
    DescribeContext context;

    auto LevelName(std::int64_t id) const -> std::string;
    // Where a rule sits: "Protection > Mythicals" ("" when it is under no heading).
    auto Place(int rule) const -> std::string;
    // The author's name for messages ("Sully"), else "the author".
    auto Author() const -> std::string;
    // Reads `text` (the session's view, or the file) and describes every rule. False when it is not a filter.
    auto Build(std::string text) -> bool;
};

// Runs a change to the player's version and saves it. The first change (no copy yet, no changes) asks first: "Make
// your own version?". `done` gets the save's outcome; `cancelled` runs when the player says no. `alive` guards the
// caller (a view that may be gone when the answer comes).
void CommitChange(Ui& ui, const std::shared_ptr<Loaded>& loaded, const std::shared_ptr<bool>& alive,
                  std::function<void(edit::Session&)> change,
                  std::function<void(Ui&, bool ok, bool switched, const std::string& error)> done, std::function<void(Ui&)> cancelled);

struct RuleDraft;

// The hovered item as the loot filter answered.
struct ItemRules {
    std::string name;           // as the game shows it, colour codes taken out
    int location = 0;           // 0 on the ground, 1 an inventory / the stash / the cube, 2 equipped, 3 at a vendor, 4 elsewhere
    bool hidden = false;        // a rule hides it (on the ground; for a carried item: if it dropped where the player is)
    bool hiddenUnmatched = false;  // hidden by the filter's hideUnmatchedItems: no rule matched
    std::string hiddenBy;       // the hiding rule's id ("R105"), "" when none or it has no id
    struct Match {
        std::string id;     // "R164", "U001"; "" when its title has no id
        std::string title;  // the rule's title, for one without an id
        int number = 0;     // its place in the game's file (1-based)
    };
    std::vector<Match> matched;  // in the filter's order, each id once
    bool more = false;           // the loot filter listed only the first ones
};


class BrowseView final : public overlay::View {
public:
    explicit BrowseView(std::shared_ptr<Source> source);
    ~BrowseView() override { *alive_ = false; }

    [[nodiscard]] auto Title() const -> std::string override;
    void Layout(const LayoutContext& context) override;
    void FocusList(std::vector<Widget*>& out) override;
    void Paint(Canvas& canvas) override;
    void HelpRows(const Widget* focused, std::vector<overlay::HelpRow>& out) const override;
    auto Key(const overlay::KeyEvent& event, Ui& ui) -> bool override;
    auto Pad(const overlay::PadEvent& event, Ui& ui) -> bool override;
    auto InitialFocus() -> Widget* override;
    auto SearchBox() -> overlay::TextBox* override { return &search_; }
    void Shown(Ui& ui) override;

    // Asks the loot filter (on the game's UI thread) which filter it has, then reads it again when it is another one
    // or the file changed. Shown does it; the plugin does it when the mod's data has been read.
    void Refresh(Ui& ui, bool force = false);
    // The second half of Refresh, with the loot filter's answer.
    void Apply(const FilterInfo& info, Ui& ui, bool force = false);
    // Opens the add-rule steps; from the hover key with the item's draft, made once the mod's data is in. Waits for
    // the filter when it is not read yet.
    using DraftMaker = std::function<RuleDraft(const gamedata::GameData&)>;
    // A new rule (the steps), or with `item` (the edit-item key on an item rules matched) what the filter does with it
    // first. Waits for the filter when it is not read yet.
    void StartRule(Ui& ui, DraftMaker makeDraft, ItemRules item = {});

    // For tests.
    [[nodiscard]] auto Data() const -> const Loaded* { return loaded_.get(); }
    [[nodiscard]] auto ShownRules() const -> const std::vector<int>& { return shown_; }
    [[nodiscard]] auto SectionRows() const -> int { return static_cast<int>(rows_.size()); }  // rows showing
    [[nodiscard]] auto SectionCount() const -> int { return static_cast<int>(sections_.size()); }  // all of them
    [[nodiscard]] auto RowName(int row) const -> std::string;
    [[nodiscard]] auto RowIsPart(int row) const -> bool;
    [[nodiscard]] auto RowExpanded(int row) const -> bool;
    [[nodiscard]] auto WhatShown(int shownIndex) const -> const std::string&;
    [[nodiscard]] auto Message() const -> const std::string& { return message_.text; }
    auto Search() -> overlay::TextBox& { return search_; }
    auto Sections() -> overlay::ListView& { return sectionList_; }
    auto Rules() -> overlay::ListView& { return ruleList_; }
    auto Filters() -> overlay::Dropdown& { return filter_; }
    [[nodiscard]] auto FilterChoices() const -> const std::vector<edit::FilterChoice>& { return filters_; }
    auto GameSettings() -> overlay::Button& { return gameSettings_; }
    auto AddRule() -> overlay::Button& { return addRule_; }
    auto Volume() -> overlay::Slider& { return volume_; }
    auto VolumeTest() -> overlay::Button& { return volumeTest_; }
    void ChooseSection(int row);  // a row of the left list, as showing
    void ToggleRow(int row);      // opens or closes a part

private:
    struct SectionRow {
        int heading = -1;  // Headings() index; -1: every rule
        bool part = false;
        int count = 0;
        std::string name;
        int parent = -1;        // a section's part (sections_ index), -1 when none
        bool expanded = false;  // a part: its sections show
        int matches = 0;        // rules that match the search
    };

    void Rebuild();          // sections, level choices and the rule list from loaded_
    void RebuildRows();      // rows_ from the parts that are open and the search
    void Refilter();         // shown_ from the section, the search and the level
    void OpenRule(int shownIndex, Ui& ui);
    void AskConflicts(Ui& ui);  // an author's update clashes with the player's changes: one question each
    [[nodiscard]] auto ActiveOnLevel(int rule) const -> bool;
    void RebuildFilters(const std::string& activeId);  // the filter drop-down from the folders
    void ChooseFilter(int index, Ui& ui);
    void WaitForFilter(const std::string& id, std::uint64_t until, Ui& ui);
    void PaintRuleRow(Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState& state);
    void SaveVolume(int volume, Ui& ui);  // the filter's soundVolume, in the player's version
    void PlayTest();                       // a sound of the filter at its volume, as the game plays it
    void PaintSectionRow(Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState& state);

    std::shared_ptr<Source> source_;
    std::shared_ptr<Loaded> loaded_;
    bool gameWasMissing_ = true;  // the data was not there at the last refresh
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);  // false once the view is gone (answers still on the way)
    std::vector<SectionRow> sections_;
    std::vector<int> rows_;   // the left list's rows: sections_ indices
    int section_ = 0;         // the chosen one, a sections_ index
    std::vector<int> shown_;
    std::vector<std::string> whatShown_;  // per shown_ rule: its items, the ones the search found first
    overlay::Highlight highlight_;

    overlay::TextBox search_;
    overlay::Label filterLabel_;
    overlay::Dropdown filter_;
    std::vector<edit::FilterChoice> filters_;
    int filterChosen_ = -1;    // the filter in use in the drop-down (the last entry opens the new-filter questions)
    std::string switching_;    // the filter asked for, until the loot filter has it
    std::string switchError_;  // the last switch failed: why
    overlay::Button gameSettings_;
    overlay::Button addRule_;
    DraftMaker pendingDraft_;  // the hover key's rule, waiting for the filter
    bool pendingRule_ = false;
    ItemRules pendingItem_;
    overlay::Label info_;
    overlay::Label volumeLabel_;
    overlay::Slider volume_;
    overlay::Label volumeValue_;
    overlay::Button volumeTest_;
    std::string volumeError_;  // the last volume save failed: why
    overlay::ListView sectionList_;
    overlay::Label count_;
    overlay::ListView ruleList_;
    overlay::Label message_;  // no filter, no data: why, in the middle
    bool empty_ = true;       // nothing to list (message_ shows)
};

}  // namespace esrfe::ui
