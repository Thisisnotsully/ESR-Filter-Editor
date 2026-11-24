// A rule's card: everything it is about, when it applies and what it does, one line each in a scrolling list, with
// its new name drawn in the game's colours. Read-only until the editing milestone.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "browse_view.hpp"

namespace esrfe::ui {

class RuleView final : public overlay::View {
public:
    RuleView(std::shared_ptr<const Loaded> loaded, int rule);

    [[nodiscard]] auto Title() const -> std::string override;
    void Layout(const LayoutContext& context) override;
    void FocusList(std::vector<Widget*>& out) override;
    void Paint(Canvas& canvas) override;
    void HelpRows(const Widget* focused, std::vector<overlay::HelpRow>& out) const override;
    auto InitialFocus() -> Widget* override { return &lines_; }

    struct Line {
        enum class Kind { Heading, Item, Preview, Note } kind = Kind::Item;
        std::string text;
    };
    [[nodiscard]] auto Lines() const -> const std::vector<Line>& { return content_; }

private:
    std::shared_ptr<const Loaded> loaded_;
    int rule_ = 0;
    std::vector<Line> content_;
    overlay::Label badge_;
    overlay::Label what_;
    overlay::ListView lines_;
    overlay::Button back_;
};

}  // namespace esrfe::ui
