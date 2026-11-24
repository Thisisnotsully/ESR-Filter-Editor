// The editor's first screen: the loaded filter, its parts and sections on the left, its rules on the right as plain
// sentences ("Hide · Flawless Amethyst and 13 more · Outside towns"), a search box over both, the filter level to
// look at (the game's own to start with), and a button to the game's own loot filter settings. A rule opens its card
// (RuleView). Read-only until the editing milestone.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "color_text.hpp"
#include "document.h"
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

// What the browse view and the rule cards share: the filter as read, and each rule in words.
struct Loaded {
    std::shared_ptr<const gamedata::GameData> game;
    gamedata::Language language = gamedata::Language::enUS;
    FilterInfo info;
    document::Document doc;
    std::vector<RuleSummary> rules;   // one per doc.Rules()
    std::vector<std::string> search;  // the search form of each rule's words, title and id
    std::vector<document::Diag> diags;
    bool parsed = false;
    std::string error;                // why the file could not be read

    auto LevelName(std::int64_t id) const -> std::string;
    // Where a rule sits: "Protection > Mythicals" ("" when it is under no heading).
    auto Place(int rule) const -> std::string;
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
    void Apply(const FilterInfo& info, bool force = false);

    // For tests.
    [[nodiscard]] auto Data() const -> const Loaded* { return loaded_.get(); }
    [[nodiscard]] auto ShownRules() const -> const std::vector<int>& { return shown_; }
    [[nodiscard]] auto SectionRows() const -> int { return static_cast<int>(sections_.size()); }
    [[nodiscard]] auto Message() const -> const std::string& { return message_.text; }
    auto Search() -> overlay::TextBox& { return search_; }
    auto Sections() -> overlay::ListView& { return sectionList_; }
    auto Rules() -> overlay::ListView& { return ruleList_; }
    auto Levels() -> overlay::Dropdown& { return level_; }
    auto GameSettings() -> overlay::Button& { return gameSettings_; }
    void ChooseSection(int row);

private:
    struct SectionRow {
        int heading = -1;  // Headings() index; -1: every rule
        bool part = false;
        int count = 0;
        std::string name;
    };

    void Rebuild();          // sections, level choices and the rule list from loaded_
    void Refilter();         // shown_ from the section, the search and the level
    void OpenRule(int shownIndex, Ui& ui);
    [[nodiscard]] auto ActiveOnLevel(int rule) const -> bool;
    [[nodiscard]] auto ChosenLevel() const -> std::int64_t;  // 0: every level
    void PaintRuleRow(Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState& state);
    void PaintSectionRow(Canvas& canvas, int index, const Box& row, const overlay::ListView::RowState& state);

    std::shared_ptr<Source> source_;
    std::shared_ptr<Loaded> loaded_;
    bool gameWasMissing_ = true;  // the data was not there at the last refresh
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);  // false once the view is gone (answers still on the way)
    std::vector<SectionRow> sections_;
    int section_ = 0;
    std::vector<int> shown_;
    overlay::Highlight highlight_;

    overlay::TextBox search_;
    overlay::Label levelLabel_;
    overlay::Dropdown level_;
    overlay::Button gameSettings_;
    overlay::Label info_;
    overlay::ListView sectionList_;
    overlay::Label count_;
    overlay::ListView ruleList_;
    overlay::Label message_;  // no filter, no data: why, in the middle
    bool empty_ = true;       // nothing to list (message_ shows)
};

}  // namespace esrfe::ui
