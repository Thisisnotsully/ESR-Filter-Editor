// The Help button: a short guide to the editor, by topic. Topics on the left; the chosen topic's paragraphs on the
// right, in a list that scrolls (mouse wheel, arrows, the D-pad).
#pragma once

#include <string>
#include <vector>

#include "view.hpp"
#include "widgets.hpp"

namespace esrfe::ui {

using overlay::Box;
using overlay::Canvas;
using overlay::LayoutContext;
using overlay::Ui;
using overlay::Widget;

struct HelpTopic {
    const char* title;
    std::vector<const char*> paragraphs;  // "#..." a heading
};

auto HelpTopics() -> const std::vector<HelpTopic>&;

class HelpView final : public overlay::View {
public:
    HelpView();

    [[nodiscard]] auto Title() const -> std::string override;
    void Layout(const LayoutContext& context) override;
    void FocusList(std::vector<Widget*>& out) override;
    auto InitialFocus() -> Widget* override { return &topics_; }

    // For tests.
    auto Topics() -> overlay::ListView& { return topics_; }
    auto Page() -> overlay::ListView& { return page_; }
    [[nodiscard]] auto Topic() const -> int { return topic_; }
    void ChooseTopic(int index);

private:
    int topic_ = 0;
    overlay::ListView topics_;
    overlay::ListView page_;
    overlay::Button back_;
};

}  // namespace esrfe::ui
