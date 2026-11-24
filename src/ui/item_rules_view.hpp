// What the filter does with an item on the ground, opened by the edit-item key: the rules that matched it (the one
// hiding it marked), each opening its card, and "Add a new rule for this item".
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "browse_view.hpp"

namespace esrfe::ui {

class ItemRulesView final : public overlay::View {
public:
    using DraftMaker = BrowseView::DraftMaker;
    ItemRulesView(std::shared_ptr<Loaded> loaded, ItemRules item, DraftMaker makeDraft);

    [[nodiscard]] auto Title() const -> std::string override;
    void Layout(const LayoutContext& context) override;
    void FocusList(std::vector<Widget*>& out) override;
    auto InitialFocus() -> Widget* override;

    struct Row {
        int rule = -1;  // loaded->doc index, -1: not in the editor's list (no id, or not found)
        std::string id;
        std::string head;  // "R164  ·  Other  ·  Hides it"
        std::string line;  // what the rule is about, or why it cannot open
        bool hides = false;
    };
    // For tests.
    [[nodiscard]] auto Rows() const -> const std::vector<Row>& { return rows_; }
    [[nodiscard]] auto State() const -> const std::string& { return state_.text; }
    auto List() -> overlay::ListView& { return list_; }
    auto AddButton() -> overlay::Button& { return add_; }
    void OpenRow(int row, Ui& ui);

private:
    std::shared_ptr<Loaded> loaded_;
    ItemRules item_;
    DraftMaker makeDraft_;
    std::vector<Row> rows_;

    overlay::Label heading_;
    overlay::Label state_;
    overlay::Label hint_;
    overlay::ListView list_;
    overlay::Button add_;
    overlay::Button back_;
};

}  // namespace esrfe::ui
