// What milestone 2 shows in game before the editor's own views exist: every control of the toolkit on one screen.
// A list of 1000 rows (with ÿc colours) filtered by a search box with highlighted matches, toggles, a check box, a
// drop-down, a number box, a text box, sort chips, and buttons that open a confirm dialog and push a second view.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "color_text.hpp"
#include "view.hpp"
#include "widgets.hpp"

namespace esrfe::overlay {

class DemoView final : public View {
public:
    explicit DemoView(int rows = 1000);

    [[nodiscard]] auto Title() const -> std::string override;
    void Layout(const LayoutContext& context) override;
    void FocusList(std::vector<Widget*>& out) override;
    void Paint(Canvas& canvas) override;
    void HelpRows(const Widget* focused, std::vector<HelpRow>& out) const override;
    auto Key(const KeyEvent& event, Ui& ui) -> bool override;
    auto Pad(const PadEvent& event, Ui& ui) -> bool override;
    auto InitialFocus() -> Widget* override { return &search_; }
    auto SearchBox() -> TextBox* override { return &search_; }

    // For tests.
    [[nodiscard]] auto ShownRows() const -> const std::vector<int>& { return shown_; }
    [[nodiscard]] auto RowText(int row) const -> const std::string& { return rows_[static_cast<std::size_t>(row)]; }
    [[nodiscard]] auto Status() const -> const std::string& { return status_; }
    auto Search() -> TextBox& { return search_; }
    auto List() -> ListView& { return list_; }
    auto DeleteButton() -> Button& { return remove_; }
    auto OpenButton() -> Button& { return open_; }
    auto Quality() -> Dropdown& { return quality_; }
    auto Level() -> NumberBox& { return level_; }
    auto Ethereal() -> Toggle& { return ethereal_; }

private:
    enum class Sort { Number, Name, NameDown };
    void Refilter();
    void SetStatus(std::string text, Ui& ui);
    void SetSort(Sort sort, Ui& ui);

    std::vector<std::string> rows_;  // "ÿc4R0001ÿc0 Harlequin Crest ..." (UTF-8)
    std::vector<std::string> searchable_;  // text::SearchTextUtf8 of each row without its colour codes
    std::vector<int> shown_;               // rows that match the search, in the sort order
    Highlight highlight_;
    Sort sort_ = Sort::Number;
    std::string status_;
    Box listPanel_{};

    TextBox search_;
    Label count_;
    ListView list_;
    Panel controls_;
    Toggle ethereal_;
    Checkbox sound_;
    Label qualityLabel_;
    Dropdown quality_;
    Label levelLabel_;
    NumberBox level_;
    Label noteLabel_;
    TextBox note_;
    Label sortLabel_;
    Chip sortNumber_, sortName_, sortNameDown_;
    Button open_;
    Button remove_;
    Label status_label_;
};

}  // namespace esrfe::overlay
