// The add-rule steps, kind of item first so a player only ever sees the choices that fit:
//   1 What happens: Hide / Show (on the levels chosen) / Always Show (every level)
//   2 What kind of item: white and superior bases, magic, rare, unique (Ancient Coupons with them), set, other
//   3 Which ones: groups and their items for bases; the unique items themselves by tier; set items by set
//   4 Tier: Normal / Exceptional / Elite, Mythicals, white or superior (bases, magic and rare items only)
//   5 Bonuses: superior bases (Superior only) and magic items, the bonuses they can roll
//   6 Filter levels (not for Always Show)
//   7 Look (Show and Always Show): border, background, name colour, light beam, drop sound (with a preview at the
//     game's volume), chat line; a picture of the label on the ground
//   8 The rule in words
// "Add rule" is there once the rule is complete. Also changes one of the player's own rules (`editingId`).
#pragma once

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "browse_view.hpp"
#include "pickers.hpp"
#include "rule_draft.h"
#include "sprite.h"

namespace esrfe::ui {

class AddRuleView final : public overlay::View {
public:
    enum class Step { Action, Kind, Items, Tier, Bonuses, Levels, Look, Check };
    static constexpr int kSteps = 8;

    AddRuleView(std::shared_ptr<Loaded> loaded, RuleDraft draft, std::string editingId);
    ~AddRuleView() override { *alive_ = false; }

    [[nodiscard]] auto Title() const -> std::string override;
    void Layout(const LayoutContext& context) override;
    void FocusList(std::vector<Widget*>& out) override;
    void Paint(Canvas& canvas) override;
    void HelpRows(const Widget* focused, std::vector<overlay::HelpRow>& out) const override;
    auto InitialFocus() -> Widget* override;
    auto Pad(const overlay::PadEvent& event, Ui& ui) -> bool override;

    // One item the items step lists.
    struct Entry {
        enum class Type { Base, Unique, Coupon, SetItem } type = Type::Base;
        std::string code;  // Base, Coupon
        int row = -1;      // Unique, SetItem
        std::string name;
        std::string tag;   // a tier, or the base a unique or set item is on
    };
    struct Category {
        std::string name;
        bool heading = false;  // a group's name above its subgroups (no list of its own)
        std::vector<Entry> entries;
    };

    // For tests.
    [[nodiscard]] auto Draft() const -> const RuleDraft& { return draft_; }
    [[nodiscard]] auto CurrentStep() const -> Step { return step_; }
    [[nodiscard]] auto StepUsed(Step step) const -> bool;
    void GoTo(Step step);
    auto StepChip(Step step) -> overlay::Chip& { return stepChips_[static_cast<std::size_t>(step)]; }
    auto ActionChip(DraftAction action) -> overlay::Chip&;
    auto KindChip(std::size_t i) -> overlay::Chip& { return kinds_[i]; }
    auto ItemSearch() -> overlay::TextBox& { return itemSearch_; }
    auto CategoryList() -> overlay::ListView& { return categoryList_; }
    [[nodiscard]] auto Categories() const -> const std::vector<Category>& { return categories_; }
    [[nodiscard]] auto Entries() const -> const std::vector<Entry>& { return entries_; }
    void ToggleEntry(int row, Ui& ui);
    auto TierBox(std::size_t i) -> overlay::Checkbox& { return tiers_[i]; }
    auto WhiteBox() -> overlay::Checkbox& { return white_; }
    auto SuperiorBox() -> overlay::Checkbox& { return superior_; }
    auto MythicalChip(std::size_t i) -> overlay::Chip& { return myth_[i]; }
    [[nodiscard]] auto StatRows() const -> const std::vector<int>& { return statRows_; }
    [[nodiscard]] auto Offered() const -> const DraftOffer& { return offer_; }
    void ToggleStat(int row, Ui& ui);
    auto LevelBox(std::size_t i) -> overlay::Checkbox& { return levels_[i]; }
    auto BorderSwatch(std::size_t i) -> overlay::Swatch& { return borderSw_[i]; }
    auto BackgroundSwatch(std::size_t i) -> overlay::Swatch& { return backSw_[i]; }
    auto NameSwatch(std::size_t i) -> overlay::Swatch& { return nameSw_[i]; }
    auto Beam() -> overlay::Checkbox& { return beam_; }
    auto BeamSwatch(std::size_t i) -> overlay::Swatch& { return beamSw_[i]; }
    auto Sound() -> overlay::Dropdown& { return sound_; }
    auto PlayButton() -> overlay::Button& { return play_; }
    auto Chat() -> overlay::Checkbox& { return chat_; }
    auto ChatText() -> overlay::TextBox& { return chatText_; }
    auto Icon() -> overlay::Dropdown& { return icon_; }
    auto IconSize() -> overlay::Slider& { return iconSize_; }
    auto Finish() -> overlay::Button& { return finish_; }
    auto Next() -> overlay::Button& { return next_; }
    [[nodiscard]] auto Status() const -> const std::string& { return status_.text; }
    [[nodiscard]] auto Summary() const -> std::string;
    [[nodiscard]] auto PreviewName() const -> std::string;  // the name the label preview shows

private:
    void Changed();
    void SyncControls();
    void ShowStep();
    void RebuildCategories();  // the items step's left list for the kind
    void RebuildEntries();     // its right list: the chosen category's items, or the search's
    void RebuildStats();
    void Save(Ui& ui);
    void PlayPreview(Ui& ui);
    void PaintLabelPreview(Canvas& canvas, const Box& box);
    void PaintMinimapPreview(Canvas& canvas, const Box& box);
    auto IconPicture(const std::string& name) -> const Picture*;  // read once, kept
    [[nodiscard]] auto Picked(const Entry& e) const -> bool;
    [[nodiscard]] auto RulesNow() const -> std::vector<edit::OwnRule>;

    std::shared_ptr<Loaded> loaded_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    const gamedata::GameData& game_;
    RuleDraft draft_;
    std::string editing_;
    Step step_ = Step::Action;
    DraftOffer offer_;
    ItemKind categoriesFor_ = ItemKind::None;
    std::vector<Category> categories_;
    std::vector<Entry> entries_;
    std::vector<int> statRows_;
    std::vector<std::int64_t> levelIds_;
    std::vector<std::string> soundFiles_;
    RuleSummary summaryNow_;
    std::vector<std::string> checkText_;
    Box previewBox_{};
    Box minimapBox_{};
    std::vector<std::string> iconNames_;
    std::map<std::string, Picture> iconPictures_;

    std::deque<overlay::Chip> stepChips_;
    overlay::Label heading_, hint_;
    overlay::Chip hide_, show_, always_;
    std::deque<overlay::Chip> kinds_;
    overlay::TextBox itemSearch_;
    overlay::ListView categoryList_, entryList_;
    overlay::Button all_, none_;
    overlay::Label chosen_;
    std::deque<overlay::Checkbox> tiers_;
    overlay::Label qualityLabel_, mythLabel_;
    overlay::Checkbox white_, superior_;
    std::deque<overlay::Chip> myth_;
    overlay::TextBox statSearch_;
    overlay::ListView statList_;
    overlay::Label atLeastLabel_;
    overlay::NumberBox atLeast_;
    std::deque<overlay::Checkbox> levels_;
    overlay::Label borderLabel_, backLabel_, nameLabel_, soundLabel_, iconLabel_, iconSizeLabel_, iconSizeValue_;
    overlay::Chip borderNone_, backNone_, nameGame_;
    std::deque<overlay::Swatch> borderSw_, backSw_, nameSw_, beamSw_, chatSw_;
    overlay::Checkbox beam_, chat_;
    overlay::Dropdown sound_;
    overlay::Button play_;
    overlay::TextBox chatText_;
    overlay::Dropdown icon_;
    overlay::Slider iconSize_;
    overlay::ListView checkLines_;
    overlay::Label summary_, status_;
    overlay::Button back_, next_, finish_;
};

}  // namespace esrfe::ui
