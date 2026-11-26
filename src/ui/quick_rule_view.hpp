// The edit-item key's screen: what the filter does with the item now, then what the player wants: hide it, change
// its style, always show it (or, for a hidden item, show it or turn off the rule hiding it); for which items, from
// this exact one out to every item of its kind; and on which strictness levels. One button makes the rule; "More
// options" opens the full steps with all of it filled in.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "browse_view.hpp"
#include "rule_scope.h"

namespace esrfe::ui {

class QuickRuleView final : public overlay::View {
public:
    enum class Want { Hide, Style, AlwaysShow, Show };
    enum class Where { ThisLevel, ThisAndStricter, Every };

    QuickRuleView(std::shared_ptr<Loaded> loaded, ItemRules item, BrowseView::DraftMaker makeDraft);

    [[nodiscard]] auto Title() const -> std::string override;
    void Layout(const LayoutContext& context) override;
    void FocusList(std::vector<Widget*>& out) override;
    auto InitialFocus() -> Widget* override;

    // For tests.
    [[nodiscard]] auto Wants() const -> std::vector<Want>;  // the actions offered, in order
    void Choose(Want want);
    void ChooseScope(int index);
    void ChooseWhere(Where where);
    [[nodiscard]] auto Scopes() const -> const std::vector<Scope>& { return scopes_; }
    [[nodiscard]] auto ScopeIndex() const -> int { return scope_; }
    [[nodiscard]] auto CurrentWant() const -> Want { return want_; }
    [[nodiscard]] auto CurrentWhere() const -> Where { return where_; }
    [[nodiscard]] auto Draft() const -> RuleDraft;  // the rule as chosen now
    [[nodiscard]] auto Note() const -> const std::string& { return note_.text; }
    [[nodiscard]] auto State() const -> const std::string& { return state_.text; }
    auto Go() -> overlay::Button& { return go_; }
    auto More() -> overlay::Button& { return more_; }
    auto TurnOff() -> overlay::Button& { return turnOff_; }
    auto SeeRules() -> overlay::Button& { return seeRules_; }
    auto WantChip(std::size_t i) -> overlay::Chip& { return wantChips_[i]; }
    auto WhereChip(std::size_t i) -> overlay::Chip& { return whereChips_[i]; }
    auto ScopeList() -> overlay::ListView& { return scopeList_; }

private:
    void Changed();
    void Save(Ui& ui);
    void OpenSteps(Ui& ui, bool style);
    [[nodiscard]] auto LevelsFor(Where where) const -> std::vector<std::int64_t>;
    [[nodiscard]] auto ShowingRules() const -> std::vector<std::string>;  // matched rules that keep it shown
    [[nodiscard]] auto StyleRules() const -> std::vector<std::string>;    // matched rules that style or announce it

    std::shared_ptr<Loaded> loaded_;
    ItemRules item_;
    BrowseView::DraftMaker makeDraft_;
    RuleDraft itemDraft_;
    std::vector<Scope> scopes_;
    std::vector<Want> wants_;
    Want want_ = Want::Hide;
    Where where_ = Where::ThisAndStricter;
    int scope_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    overlay::Label heading_;
    overlay::Label state_;
    overlay::Button seeRules_;
    overlay::Label wantLabel_;
    std::vector<overlay::Chip> wantChips_;
    overlay::Button turnOff_;
    overlay::Label scopeLabel_;
    overlay::ListView scopeList_;
    overlay::Label whereLabel_;
    std::vector<overlay::Chip> whereChips_;
    overlay::Label note_;
    overlay::Label status_;
    overlay::Button back_;
    overlay::Button more_;
    overlay::Button go_;

public:
    ~QuickRuleView() override { *alive_ = false; }
};

}  // namespace esrfe::ui
