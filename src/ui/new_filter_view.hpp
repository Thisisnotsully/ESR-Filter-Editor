// The new-filter wizard: a filter from the ground up, step by step (name, levels, what is always shown, what is
// hidden and when, announcements and labels, label colours), every step filled in from the starter (Sully's QoL
// Filter's choices) so "Next" through to "Create" gives a working filter.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "new_filter.h"
#include "pickers.hpp"
#include "source.h"
#include "view.hpp"
#include "widgets.hpp"

namespace esrfe::ui {

using overlay::Box;
using overlay::Canvas;
using overlay::LayoutContext;
using overlay::Ui;
using overlay::Widget;

class NewFilterView final : public overlay::View {
public:
    // `created`: the filter is made and the game switched to it ("user:<name>.json").
    NewFilterView(std::shared_ptr<Source> source, std::function<void(Ui&, const std::string& mergedId)> created);

    [[nodiscard]] auto Title() const -> std::string override;
    void Layout(const LayoutContext& context) override;
    void FocusList(std::vector<Widget*>& out) override;
    void Paint(Canvas& canvas) override;
    auto InitialFocus() -> Widget* override;
    auto Pad(const overlay::PadEvent& event, Ui& ui) -> bool override;

    // A line of a part step's list: a part, or one of its rules (a step).
    struct Line {
        int part = -1;  // starter_.parts index
        int row = -1;   // the part's row in the recipe, -1: the part itself
    };

    // For tests.
    [[nodiscard]] auto Ready() const -> bool { return ready_; }
    [[nodiscard]] auto Problem() const -> const std::string& { return problem_.text; }
    [[nodiscard]] auto StepCount() const -> int { return static_cast<int>(starter_.steps.size()); }
    [[nodiscard]] auto CurrentStep() const -> int { return step_; }
    void GoTo(int step);
    [[nodiscard]] auto StepKey() const -> std::string;
    [[nodiscard]] auto Answers() const -> const edit::Recipe& { return recipe_; }
    [[nodiscard]] auto Lines() const -> const std::vector<Line>& { return lines_; }
    [[nodiscard]] auto LineText(int line) const -> std::string;
    auto List() -> overlay::ListView& { return list_; }
    void ToggleLine(int line, Ui& ui);
    auto Name() -> overlay::TextBox& { return name_; }
    auto Author() -> overlay::TextBox& { return author_; }
    auto StartChip(int i) -> overlay::Chip& { return start_[static_cast<std::size_t>(i)]; }
    auto CountChip(int i) -> overlay::Chip& { return counts_[static_cast<std::size_t>(i)]; }
    auto LevelName(int i) -> overlay::TextBox& { return levelNames_[static_cast<std::size_t>(i)]; }
    auto DefaultLevel() -> overlay::Dropdown& { return defaultLevel_; }
    auto RowOn() -> overlay::Checkbox& { return rowOn_; }
    auto From() -> overlay::Dropdown& { return from_; }
    auto To() -> overlay::Dropdown& { return to_; }
    auto Clvl() -> overlay::NumberBox& { return clvl_; }
    auto Amount() -> overlay::NumberBox& { return amount_; }
    auto Towns() -> overlay::Checkbox& { return towns_; }
    auto AddStep() -> overlay::Button& { return addStep_; }
    auto RemoveStep() -> overlay::Button& { return removeStep_; }
    auto PartOn() -> overlay::Toggle& { return partOn_; }
    auto Styles() -> overlay::ListView& { return styles_; }
    auto BorderSwatch(std::size_t i) -> overlay::Swatch& { return borderSw_[i]; }
    auto BeamSwatch(std::size_t i) -> overlay::Swatch& { return beamSw_[i]; }
    auto Volume() -> overlay::Slider& { return volume_; }
    auto TestSound() -> overlay::Button& { return test_; }
    auto Next() -> overlay::Button& { return next_; }
    auto Create() -> overlay::Button& { return create_; }
    [[nodiscard]] auto CheckLines() const -> const std::vector<std::string>& { return checkText_; }

private:
    void Changed();       // the answers changed: the lists, the controls, the summary
    void SyncControls();  // the controls from the answers
    void ShowStep();      // what this step shows
    void RebuildLines();  // lines_ for this step's parts
    void RebuildCheck();
    void SetLevelCount(int count);
    void Save(Ui& ui);
    void PlayTest();
    [[nodiscard]] auto SelectedLine() const -> const Line*;
    [[nodiscard]] auto RowAt(const Line& line) -> edit::Row*;
    [[nodiscard]] auto PartChoice(int part) -> edit::PartChoice*;
    [[nodiscard]] auto RowSummary(const edit::Row& row, const edit::StarterPart& part) const -> std::string;
    [[nodiscard]] auto RuleTitle(const std::string& id) const -> std::string;
    [[nodiscard]] auto LevelCount() const -> int { return static_cast<int>(recipe_.levels.size()); }
    [[nodiscard]] auto CurrentLook() const -> edit::LookChoice;  // the chosen style, the player's colours over the starter's
    void SetLook(int what, const edit::Rgb3* color);              // 0 border, 1 background, 2 beam; nullptr: as it is

    std::shared_ptr<Source> source_;
    std::function<void(Ui&, const std::string&)> created_;
    edit::Starter starter_;
    edit::Recipe recipe_;
    bool ready_ = false;
    int step_ = 0;
    std::vector<Line> lines_;
    std::vector<std::string> checkText_;
    Box previewBox_{};

    std::vector<overlay::Chip> stepChips_;
    overlay::Label heading_;
    overlay::Label hint_;
    overlay::Label problem_;
    // Name.
    overlay::Label nameLabel_, authorLabel_, startLabel_;
    overlay::TextBox name_, author_;
    std::vector<overlay::Chip> start_;  // Recommended, Minimal
    // Levels.
    overlay::Label countLabel_, defaultLabel_;
    std::vector<overlay::Chip> counts_;  // 1-5
    std::vector<overlay::Label> levelLabels_;
    std::vector<overlay::TextBox> levelNames_;
    overlay::Dropdown defaultLevel_;
    // Parts.
    overlay::ListView list_;
    overlay::Label partTitle_, partHelp_;
    overlay::Toggle partOn_;
    overlay::Checkbox rowOn_;
    overlay::Label fromLabel_, toLabel_, clvlLabel_, amountLabel_;
    overlay::Dropdown from_, to_;
    overlay::NumberBox clvl_, amount_;
    overlay::Checkbox towns_;
    overlay::Button addStep_, removeStep_;
    // Look.
    overlay::ListView styles_;
    overlay::Label borderLabel_, backLabel_, beamLabel_, volumeLabel_;
    overlay::Chip borderSame_, backSame_, beamSame_;
    std::vector<overlay::Swatch> borderSw_, backSw_, beamSw_;
    overlay::Slider volume_;
    overlay::Label volumeValue_;
    overlay::Button test_;
    // Check.
    overlay::ListView checkList_;
    // Footer.
    overlay::Button back_, next_, create_;
};

}  // namespace esrfe::ui
