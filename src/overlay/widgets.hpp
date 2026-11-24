// The toolkit the editor's views are drawn with, carved from what the wiki overlay draws: its buttons, chips, list rows,
// search box, scroll bar, number fields and drop-downs, with the same colours and shadows. A view owns its widgets,
// sets their boxes in View::Layout and lists them in View::FocusList; the Form (form.hpp) routes the mouse, the
// keyboard and the controller to them and draws the focus frame, the keyboard, the choice lists, dialogs and tooltips.
//
// No Windows here: widgets draw through Canvas and ask for services through Ui, so they run in the Linux tests.
// All text is UTF-8; labels are English and go through UiText when drawn (data text: set `translate` false).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "canvas.hpp"
#include "geometry.hpp"
#include "input.hpp"
#include "list_math.hpp"
#include "text_edit.hpp"
#include "ui.hpp"

namespace esrfe::overlay {

struct PaintState {
    bool focused = false;  // has the focus (the Form draws the focus frame itself)
    bool hovered = false;  // the pointer is over it
    bool pressed = false;  // the mouse button went down on it and is still held
    bool padActive = false;
};

class Widget {
public:
    Box box;
    bool visible = true;
    bool enabled = true;
    std::string tooltip;  // shown beside it on hover, and on controller focus (English or data text; not translated)

    virtual ~Widget() = default;
    [[nodiscard]] virtual auto Focusable() const -> bool { return false; }
    virtual void Paint(Canvas& canvas, const PaintState& state) = 0;
    // The box the focus frame goes round (a list: its selected row).
    [[nodiscard]] virtual auto FocusBox() const -> Box { return box; }
    [[nodiscard]] virtual auto HitTest(int x, int y) const -> bool { return box.Contains(x, y); }
    // Mouse input in client pixels: Down when the press is on it, Move while hovered or captured, Up when captured.
    virtual auto Mouse(const MouseEvent& event, Ui& ui) -> bool;
    virtual auto Wheel(int steps, int x, int y, Ui& ui) -> bool;
    virtual auto Key(const KeyEvent& event, Ui& ui) -> bool;
    virtual auto Char(char16_t c, Ui& ui) -> bool;
    // Controller presses while it has the focus. Returning false for a direction lets the focus move on.
    virtual auto Pad(const PadEvent& event, Ui& ui) -> bool;
    virtual auto ScrollBy(int pixels, Ui& ui) -> bool;  // the right stick and the triggers
    virtual void HelpRows(std::vector<HelpRow>& out) const;
    // The tooltip at a point (lists give one per row) and its anchor box; empty: none.
    virtual auto TooltipAt(int x, int y, Box& anchor) const -> std::string;
    virtual auto FocusTooltip(Box& anchor) const -> std::string;
    virtual void FocusChanged(bool focused, Ui& ui);
    // Mouse left the widget (hover ends).
    virtual void Leave(Ui& ui);
    // The controller's cursor was left at (x, y) on it and the D-pad was pressed: a list selects the row there.
    virtual void PointFocus(int x, int y, Ui& ui);
    // The text box this widget types into (the IME and the on-screen keyboard use it), or nullptr.
    virtual auto TextInput() -> TextBox* { return nullptr; }
};

// Text, with ÿc colours. Not focusable.
class Label : public Widget {
public:
    std::string text;
    Font font = Font::Body;
    Rgb color{};             // all zero: the theme's body colour
    bool useThemeColor = true;
    unsigned flags = text_flags::kVCenter | text_flags::kSingleLine;
    bool translate = true;  // UiText(text) when drawn
    bool heading = false;   // drawn in the heading gold with a shadow (the wiki's title)
    void Paint(Canvas& canvas, const PaintState& state) override;
};

// A panel: the wiki's see-through dark fill with a bronze frame and a corner shadow. Not focusable.
class Panel : public Widget {
public:
    std::string title;  // drawn in the heading gold at the top left, when set
    bool frame = true;
    void Paint(Canvas& canvas, const PaintState& state) override;
};

class Button : public Widget {
public:
    enum class Style {
        Normal,   // the wiki's buttons (< Prev, Next >, X): dark fill, bronze frame
        Primary,  // the gold "PLAY" button (the wiki's switch buttons)
        Danger,   // a red frame and text, for deleting
    };
    std::string label;
    Style style = Style::Normal;
    bool translate = true;
    Font font = Font::Body;
    std::function<void(Ui&)> onClick;

    [[nodiscard]] auto Focusable() const -> bool override { return visible && enabled; }
    void Paint(Canvas& canvas, const PaintState& state) override;
    auto Mouse(const MouseEvent& event, Ui& ui) -> bool override;
    auto Key(const KeyEvent& event, Ui& ui) -> bool override;
    auto Pad(const PadEvent& event, Ui& ui) -> bool override;
    void HelpRows(std::vector<HelpRow>& out) const override;
    void Activate(Ui& ui);
};

// A filter or sort button (the wiki's chips): drawn selected or not; pressing calls onClick, which usually selects it.
class Chip : public Button {
public:
    bool selected = false;
    Chip() { font = Font::Small; }
    void Paint(Canvas& canvas, const PaintState& state) override;
};

// An on / off switch with its label on the left.
class Toggle : public Widget {
public:
    std::string label;
    bool on = false;
    bool translate = true;
    std::function<void(bool, Ui&)> onChange;

    [[nodiscard]] auto Focusable() const -> bool override { return visible && enabled; }
    void Paint(Canvas& canvas, const PaintState& state) override;
    auto Mouse(const MouseEvent& event, Ui& ui) -> bool override;
    auto Key(const KeyEvent& event, Ui& ui) -> bool override;
    auto Pad(const PadEvent& event, Ui& ui) -> bool override;
    void HelpRows(std::vector<HelpRow>& out) const override;
    void Flip(Ui& ui);
};

// A box to tick, with its label on the right.
class Checkbox : public Toggle {
public:
    void Paint(Canvas& canvas, const PaintState& state) override;
};

// One line of text to type: caret, selection, clipboard, the IME (through WM_CHAR), and the on-screen keyboard with a
// controller. The search style adds the wiki search box's clear button and its B rule (B on the keyboard clears the
// text, then closes the keyboard).
class TextBox : public Widget {
public:
    std::string placeholder;  // English, translated when drawn
    bool search = false;
    bool keyboardExitDown = true;  // D-pad down from the keyboard's bottom row leaves the keyboard, focus moving down
    TextEdit edit;
    std::function<void(const std::string&, Ui&)> onChange;
    std::function<void(const std::string&, Ui&)> onSubmit;  // Enter

    [[nodiscard]] auto Value() const -> std::string;
    void SetValue(const std::string& utf8);  // does not call onChange
    // Changes made from outside (the on-screen keyboard): onChange and a repaint.
    void Changed(Ui& ui);
    [[nodiscard]] auto CaretBox(Measurer& measure) const -> Box;  // for the IME's composition window
    [[nodiscard]] auto ClearBox() const -> Box;                    // the X of a search box with text

    [[nodiscard]] auto Focusable() const -> bool override { return visible && enabled; }
    void Paint(Canvas& canvas, const PaintState& state) override;
    auto Mouse(const MouseEvent& event, Ui& ui) -> bool override;
    auto Key(const KeyEvent& event, Ui& ui) -> bool override;
    auto Char(char16_t c, Ui& ui) -> bool override;
    auto Pad(const PadEvent& event, Ui& ui) -> bool override;
    void HelpRows(std::vector<HelpRow>& out) const override;
    void FocusChanged(bool focused, Ui& ui) override;
    auto TextInput() -> TextBox* override { return this; }

private:
    [[nodiscard]] auto TextArea() const -> Box;
    auto PositionAtX(int x, Measurer& measure) const -> std::size_t;
    int scroll_ = 0;  // pixels of text scrolled off the left
    bool selecting_ = false;
};

// A whole number with - and + on either side (the wiki calculator's number fields): the D-pad's left / right or the
// buttons step it (1, then 5, then 10 while held, pad::NumberStep), the wheel steps it, digits type a new value (Enter
// or leaving the field keeps it, Escape goes back), X (Square) sets it to 0 or the nearest allowed value.
class NumberBox : public Widget {
public:
    int value = 0;
    int minimum = 0;
    int maximum = 100;
    int step = 1;
    std::function<void(int, Ui&)> onChange;

    void SetValue(int v);  // clamped, does not call onChange
    [[nodiscard]] auto MinusBox() const -> Box;
    [[nodiscard]] auto PlusBox() const -> Box;
    [[nodiscard]] auto Typing() const -> bool { return typing_; }

    [[nodiscard]] auto Focusable() const -> bool override { return visible && enabled; }
    void Paint(Canvas& canvas, const PaintState& state) override;
    auto Mouse(const MouseEvent& event, Ui& ui) -> bool override;
    auto Wheel(int steps, int x, int y, Ui& ui) -> bool override;
    auto Key(const KeyEvent& event, Ui& ui) -> bool override;
    auto Char(char16_t c, Ui& ui) -> bool override;
    auto Pad(const PadEvent& event, Ui& ui) -> bool override;
    void HelpRows(std::vector<HelpRow>& out) const override;
    void FocusChanged(bool focused, Ui& ui) override;

    void StepBy(int steps, Ui& ui);  // `steps` times `step`, clamped; onChange when it changed

private:
    void Commit(Ui& ui);
    bool typing_ = false;
    TextEdit edit_;
    int holdRepeats_ = 0;  // presses in this hold of the D-pad (for the step size)
};

// A choice from a list: shows the chosen one; A / Enter / a click opens the list over everything (the wiki
// calculator's drop-downs). The open list is drawn and driven by the Form through the Popup* functions.
class Dropdown : public Widget {
public:
    std::vector<std::string> options;
    int selected = -1;
    bool translate = true;  // UiText on the options
    int maxRows = 8;
    std::function<void(int, Ui&)> onChange;

    [[nodiscard]] auto SelectedText() const -> std::string;
    [[nodiscard]] auto Focusable() const -> bool override { return visible && enabled; }
    void Paint(Canvas& canvas, const PaintState& state) override;
    auto Mouse(const MouseEvent& event, Ui& ui) -> bool override;
    auto Wheel(int steps, int x, int y, Ui& ui) -> bool override;
    auto Key(const KeyEvent& event, Ui& ui) -> bool override;
    auto Pad(const PadEvent& event, Ui& ui) -> bool override;
    void HelpRows(std::vector<HelpRow>& out) const override;

    // ---- the open list ----
    void PopupOpened(const Box& client, Measurer& measure);  // places the list and highlights the chosen row
    void PopupPlace(const Box& client, Measurer& measure);   // places it again (the window changed), keeping the highlight
    [[nodiscard]] auto PopupBox() const -> Box { return popupBox_; }
    void PaintPopup(Canvas& canvas);
    auto PopupMouse(const MouseEvent& event, Ui& ui) -> bool;  // false: the click was outside (the list closes)
    auto PopupWheel(int steps, Ui& ui) -> bool;
    auto PopupKey(const KeyEvent& event, Ui& ui) -> bool;
    auto PopupPad(const PadEvent& event, Ui& ui) -> bool;
    [[nodiscard]] auto PopupHighlight() const -> int { return highlight_; }
    void Choose(int index, Ui& ui);  // sets it, closes the list, onChange when it changed

private:
    [[nodiscard]] auto RowBox(int index) const -> Box;
    void MoveHighlight(int delta);
    Box popupBox_{};
    int rowHeight_ = 24;
    int visibleRows_ = 8;
    int popupScroll_ = 0;  // the first row showing
    int highlight_ = -1;
};

// A list with one row per item, drawn only where it shows (the editor lists 729 rules), with the wiki's list rows and
// scroll bar. The rows are fixed height; `paintRow` draws a row's content over the row background.
class ListView : public Widget {
public:
    struct RowState {
        bool selected = false;
        bool hovered = false;
        bool focused = false;  // the list has the focus
    };
    int count = 0;
    int rowHeight = 26;
    int rowGap = 4;  // between rows (the wiki's tight spacing); part of each row's pitch
    std::string emptyText = "No matches";  // English
    std::function<void(Canvas&, int, const Box&, const RowState&)> paintRow;
    std::function<void(int, Ui&)> onSelect;    // the selection moved (mouse, keys, controller)
    std::function<void(int, Ui&)> onActivate;  // Enter, A, or a double click
    std::function<void(int, Ui&)> onClick;     // a single click on a row (after it is selected)
    std::function<std::string(int)> rowTooltip;
    bool drawRowBackground = true;  // the wiki's row: corner shadow, dark fill, gold frame when selected

    [[nodiscard]] auto Selected() const -> int { return selected_; }
    void SetCount(int rows);            // keeps the selection when it still exists
    void Select(int index, Ui* ui);     // scrolls it into view; onSelect when ui is given and it changed
    [[nodiscard]] auto Scroll() const -> int { return scroll_; }
    void SetScroll(int pixels);
    [[nodiscard]] auto RowBox(int index) const -> Box;  // where the row is now (may be outside the box)
    [[nodiscard]] auto ViewBox() const -> Box;          // the rows' area (the box without the scroll bar)
    [[nodiscard]] auto Pitch() const -> int { return rowHeight + rowGap; }
    [[nodiscard]] auto PageRows() const -> int;
    [[nodiscard]] auto Visible() const -> RowRange;

    [[nodiscard]] auto Focusable() const -> bool override { return visible && enabled; }
    void Paint(Canvas& canvas, const PaintState& state) override;
    [[nodiscard]] auto FocusBox() const -> Box override;
    auto Mouse(const MouseEvent& event, Ui& ui) -> bool override;
    auto Wheel(int steps, int x, int y, Ui& ui) -> bool override;
    auto Key(const KeyEvent& event, Ui& ui) -> bool override;
    auto Pad(const PadEvent& event, Ui& ui) -> bool override;
    auto ScrollBy(int pixels, Ui& ui) -> bool override;
    void HelpRows(std::vector<HelpRow>& out) const override;
    auto TooltipAt(int x, int y, Box& anchor) const -> std::string override;
    auto FocusTooltip(Box& anchor) const -> std::string override;
    void Leave(Ui& ui) override;
    void PointFocus(int x, int y, Ui& ui) override;
    void FocusChanged(bool focused, Ui& ui) override;

private:
    [[nodiscard]] auto TrackBox() const -> Box;
    [[nodiscard]] auto ThumbNow() const -> Thumb;
    void Move(int delta, Ui& ui);
    int selected_ = -1;
    int scroll_ = 0;
    int hover_ = -1;
    int barColumn_ = 18;  // the scroll bar's column, S(18), from the last paint
    int minThumb_ = 28;
    bool dragging_ = false;
    int dragStartY_ = 0;
    int dragStartScroll_ = 0;
};

// The wiki's list row background: corner shadow, dark see-through fill (selected: brown), gold frame when selected.
void PaintRowBackground(Canvas& canvas, const Box& row, bool selected, bool hovered);
// The wiki's focus frame: two frames just outside the box in the focus colour.
void PaintFocusFrame(Canvas& canvas, const Box& box);

}  // namespace esrfe::overlay
