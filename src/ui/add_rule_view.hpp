// The add-rule steps: 1 what happens (hide / always show / highlight, its colour, beam and chat line), 2 which items
// (the game's groups and their bases, or a search), 3 tier (and Mythicals: with them, without them, only them),
// 4 rarity (only the ones the items come in), 5 stats (Superior or Magic: only the bonuses the items can roll; the
// rule then needs the item identified), 6 filter levels, 7 the rule in words. "Add rule" is there from the moment the
// rule is complete. Also changes one of the player's own rules (`editingId`).
#pragma once

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "browse_view.hpp"
#include "rule_draft.h"

namespace esrfe::ui {

class AddRuleView final : public overlay::View {
public:
    enum class Step { Action, Items, Tier, Rarity, Stats, Levels, Check };
    static constexpr int kSteps = 7;

    AddRuleView(std::shared_ptr<Loaded> loaded, RuleDraft draft, std::string editingId);
    ~AddRuleView() override { *alive_ = false; }

    [[nodiscard]] auto Title() const -> std::string override;
    void Layout(const LayoutContext& context) override;
    void FocusList(std::vector<Widget*>& out) override;
    void Paint(Canvas& canvas) override;
    void HelpRows(const Widget* focused, std::vector<overlay::HelpRow>& out) const override;
    auto InitialFocus() -> Widget* override;
    auto Pad(const overlay::PadEvent& event, Ui& ui) -> bool override;

    // For tests.
    [[nodiscard]] auto Draft() const -> const RuleDraft& { return draft_; }
    [[nodiscard]] auto CurrentStep() const -> Step { return step_; }
    void GoTo(Step step);
    auto StepChip(Step step) -> overlay::Chip& { return stepChips_[static_cast<std::size_t>(step)]; }
    auto HideChip() -> overlay::Chip& { return hide_; }
    auto ShowChip() -> overlay::Chip& { return show_; }
    auto HighlightChip() -> overlay::Chip& { return highlight_; }
    auto ColorChip(std::size_t i) -> overlay::Chip& { return colors_[i]; }
    auto Beam() -> overlay::Checkbox& { return beam_; }
    auto ItemSearch() -> overlay::TextBox& { return itemSearch_; }
    auto Groups() -> overlay::ListView& { return groupList_; }
    auto BaseList() -> overlay::ListView& { return baseList_; }
    [[nodiscard]] auto BaseRows() const -> const std::vector<std::string>& { return baseRows_; }
    [[nodiscard]] auto GroupRowName(int row) const -> std::string;
    void ToggleBase(int row, Ui& ui);
    auto TierBox(std::size_t i) -> overlay::Checkbox& { return tiers_[i]; }
    auto MythicalChip(MythicalMode mode) -> overlay::Chip& { return myth_[static_cast<std::size_t>(mode)]; }
    auto RarityBox(std::size_t i) -> overlay::Checkbox& { return rarities_[i]; }
    auto StatList() -> overlay::ListView& { return statList_; }
    [[nodiscard]] auto StatRows() const -> const std::vector<int>& { return statRows_; }
    [[nodiscard]] auto Offered() const -> const DraftOffer& { return offer_; }
    void ToggleStat(int row, Ui& ui);
    auto AtLeast() -> overlay::NumberBox& { return atLeast_; }
    auto LevelBox(std::size_t i) -> overlay::Checkbox& { return levels_[i]; }
    auto Finish() -> overlay::Button& { return finish_; }
    auto Next() -> overlay::Button& { return next_; }
    [[nodiscard]] auto Status() const -> const std::string& { return status_.text; }
    [[nodiscard]] auto Summary() const -> std::string;  // the rule in one line

private:
    void Changed();          // the draft changed: offer, controls, summary
    void SyncControls();
    void ShowStep();         // which widgets the step shows
    void RebuildBases();     // the right list: the chosen group's bases or the search's
    void RebuildStats();
    void Save(Ui& ui);
    [[nodiscard]] auto StepUsed(Step step) const -> bool;  // a step with something to choose
    [[nodiscard]] auto RuleNow() const -> edit::OwnRule;

    struct GroupRow {
        int group = -1;
        int subgroup = -1;   // -1: a group's heading (not a list of its own)
        std::string name;
    };

    std::shared_ptr<Loaded> loaded_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    const gamedata::GameData& game_;
    RuleDraft draft_;
    std::string editing_;
    Step step_ = Step::Action;
    DraftOffer offer_;
    std::vector<GroupRow> groupRows_;
    std::vector<std::string> baseRows_;
    std::vector<int> statRows_;  // offer_.stats indices shown
    std::vector<std::int64_t> levelIds_;
    RuleSummary summaryNow_;

    std::deque<overlay::Chip> stepChips_;
    overlay::Label heading_;
    overlay::Label hint_;
    overlay::Chip hide_, show_, highlight_;
    overlay::Label colorLabel_;
    std::deque<overlay::Chip> colors_;
    overlay::Checkbox beam_, chat_;
    overlay::TextBox itemSearch_;
    overlay::ListView groupList_;
    overlay::ListView baseList_;
    overlay::Button all_, none_;
    overlay::Label chosen_;
    std::deque<overlay::Checkbox> tiers_;  // Normal, Exceptional, Elite
    overlay::Label mythLabel_;
    std::deque<overlay::Chip> myth_;       // with, without, only
    std::deque<overlay::Checkbox> rarities_;
    overlay::TextBox statSearch_;
    overlay::ListView statList_;
    overlay::Label atLeastLabel_;
    overlay::NumberBox atLeast_;
    std::deque<overlay::Checkbox> levels_;
    overlay::ListView checkLines_;
    std::vector<std::string> checkText_;
    overlay::Label summary_;
    overlay::Label status_;
    overlay::Button back_, next_, finish_;
};

}  // namespace esrfe::ui
