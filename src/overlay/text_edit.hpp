// A single line of editable text: caret, selection, and the edits a text box makes, in UTF-16 (what WM_CHAR delivers).
// No Windows: the text box widget and its tests use it. Positions are UTF-16 indices and never split a surrogate pair.
//
// WM_CHAR delivers a character outside the BMP (an emoji, a rare CJK ideograph, also what an IME commits) as two
// messages, the high surrogate first; TypeChar keeps the high half until the low half arrives.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace esrfe::overlay {

class TextEdit {
public:
    enum class Filter {
        Any,      // every printable character
        Integer,  // digits, and a '-' at the start
    };

    [[nodiscard]] auto Text() const -> const std::u16string& { return text_; }
    [[nodiscard]] auto Caret() const -> std::size_t { return caret_; }
    [[nodiscard]] auto Anchor() const -> std::size_t { return anchor_; }
    [[nodiscard]] auto HasSelection() const -> bool { return caret_ != anchor_; }
    [[nodiscard]] auto SelectionStart() const -> std::size_t { return caret_ < anchor_ ? caret_ : anchor_; }
    [[nodiscard]] auto SelectionEnd() const -> std::size_t { return caret_ < anchor_ ? anchor_ : caret_; }
    [[nodiscard]] auto SelectedText() const -> std::u16string;

    void SetMaxLength(std::size_t units) { maxLength_ = units; }
    void SetFilter(Filter filter) { filter_ = filter; }
    [[nodiscard]] auto MaxLength() const -> std::size_t { return maxLength_; }

    // Replaces the text; the caret goes to the end (everything selected when `selectAll`).
    void SetText(std::u16string_view text, bool selectAll = false);

    // Each returns true when the text changed.
    auto TypeChar(char16_t c) -> bool;                  // one WM_CHAR unit (control characters are ignored)
    auto Insert(std::u16string_view text) -> bool;      // replaces the selection (paste, on-screen keyboard)
    auto Backspace(bool word = false) -> bool;          // the selection, or the code point (word) before the caret
    auto Delete(bool word = false) -> bool;             // the selection, or the code point (word) after the caret
    auto DeleteSelection() -> bool;
    auto Clear() -> bool;

    // Caret moves; with `extend` the selection grows from its anchor, otherwise it collapses.
    void Left(bool extend = false, bool word = false);
    void Right(bool extend = false, bool word = false);
    void Home(bool extend = false);
    void End(bool extend = false);
    void SelectAll();
    void SetCaret(std::size_t position, bool extend = false);

    // The caret position nearest to x (pixels from the text's left edge), given the width of a prefix of the text.
    [[nodiscard]] auto PositionAt(int x, const std::function<int(std::u16string_view)>& prefixWidth) const -> std::size_t;

    // The horizontal scroll (pixels) that keeps the caret inside a box `width` pixels wide, from the current `scroll`.
    [[nodiscard]] auto ScrollForCaret(int scroll, int width, const std::function<int(std::u16string_view)>& prefixWidth) const -> int;

private:
    [[nodiscard]] auto Accepts(char16_t c, std::size_t at) const -> bool;
    [[nodiscard]] auto PrevPoint(std::size_t at) const -> std::size_t;
    [[nodiscard]] auto NextPoint(std::size_t at) const -> std::size_t;
    [[nodiscard]] auto PrevWord(std::size_t at) const -> std::size_t;
    [[nodiscard]] auto NextWord(std::size_t at) const -> std::size_t;
    [[nodiscard]] auto Snap(std::size_t at) const -> std::size_t;  // off the middle of a surrogate pair

    std::u16string text_;
    std::size_t caret_ = 0;
    std::size_t anchor_ = 0;
    std::size_t maxLength_ = 1024;
    Filter filter_ = Filter::Any;
    char16_t pendingHigh_ = 0;
};

}  // namespace esrfe::overlay
