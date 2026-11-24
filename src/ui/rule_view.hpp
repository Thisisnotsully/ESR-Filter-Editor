// A rule's card: on the left everything it is about, when it applies and what it does (its new name drawn in the
// game's colours); on the right the player's changes to it: on or off, what it does (hide / always show / neither),
// the filter levels it is used on, and the items it is about (untick one to leave it alone). Every change is saved at
// once into the player's own version of the filter; the first one asks, and switches the game to that version.
#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "browse_view.hpp"

namespace esrfe::ui {

class RuleView final : public overlay::View {
public:
    RuleView(std::shared_ptr<Loaded> loaded, int rule);
    ~RuleView() override { *alive_ = false; }

    [[nodiscard]] auto Title() const -> std::string override;
    void Layout(const LayoutContext& context) override;
    void FocusList(std::vector<Widget*>& out) override;
    void Paint(Canvas& canvas) override;
    void HelpRows(const Widget* focused, std::vector<overlay::HelpRow>& out) const override;
    auto InitialFocus() -> Widget* override;

    struct Line {
        enum class Kind { Heading, Item, Preview, Note } kind = Kind::Item;
        std::string text;
    };
    struct Entry {  // one item of the rule's lists, as the author wrote it
        std::string key;  // itemCodes, itemTypes, uniqueOrSetIds
        document::Value value;
        std::string name;
        bool on = true;  // not taken out by the player
    };

    // For tests.
    [[nodiscard]] auto Lines() const -> const std::vector<Line>& { return content_; }
    [[nodiscard]] auto Status() const -> const std::string& { return status_.text; }
    [[nodiscard]] auto Editable() const -> bool { return editable_; }
    auto OnSwitch() -> overlay::Toggle& { return on_; }
    auto HideChip() -> overlay::Chip& { return hide_; }
    auto ShowChip() -> overlay::Chip& { return show_; }
    auto NeitherChip() -> overlay::Chip& { return neither_; }
    auto LevelBox(std::size_t i) -> overlay::Checkbox& { return levels_[i]; }
    [[nodiscard]] auto LevelCount() const -> std::size_t { return levels_.size(); }
    auto Items() -> overlay::ListView& { return items_; }
    [[nodiscard]] auto ItemEntries() const -> const std::vector<Entry>& { return entries_; }
    auto ResetButton() -> overlay::Button& { return reset_; }
    auto ChangeButton() -> overlay::Button& { return change_; }
    auto DeleteButton() -> overlay::Button& { return delete_; }
    [[nodiscard]] auto Own() const -> bool { return own_; }
    void ToggleItem(int index, Ui& ui);

private:
    enum class Act { Hide, Show, Neither };
    void Describe();      // summary_ and content_ from the rule as the player has it
    void SyncControls();  // the controls from the rule as the player has it
    void SetAction(Act act, Ui& ui);
    void SetLevel(std::size_t index, bool on, Ui& ui);
    // Runs the edit and saves; the first time, asks before the player's own version is made.
    void Commit(Ui& ui, std::function<void(edit::Session&)> change);
    void Saved(bool ok, bool switched, const std::string& error);
    [[nodiscard]] auto Current() const -> document::Value;  // the rule as the player has it

    std::shared_ptr<Loaded> loaded_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    int rule_ = 0;
    std::string id_;
    bool editable_ = false;     // an author's rule and a session to save it
    bool own_ = false;          // one of the player's own rules (U###)
    bool actionShown_ = false;  // the rule hides or shows (the action chips apply)
    RuleSummary summary_;
    std::vector<Line> content_;
    std::vector<Entry> entries_;
    std::vector<std::int64_t> levelIds_;

    overlay::Label badge_;
    overlay::Label what_;
    overlay::ListView lines_;
    overlay::Panel panel_;
    overlay::Toggle on_;
    overlay::Label actionLabel_;
    overlay::Chip hide_, show_, neither_;
    overlay::Label levelsLabel_;
    std::deque<overlay::Checkbox> levels_;
    overlay::Label itemsLabel_;
    overlay::ListView items_;
    overlay::Button reset_;
    overlay::Button change_;  // own rules: the add-rule steps again
    overlay::Button delete_;  // own rules
    overlay::Label status_;
    overlay::Label note_;  // why nothing can be changed here
    overlay::Button back_;
};

}  // namespace esrfe::ui
