#include "text_edit.hpp"

#include <algorithm>

#include "utf.hpp"

namespace esrfe::overlay {
namespace {

auto IsSpace(char16_t c) -> bool { return c == u' ' || c == u'\t' || c == 0x3000 || c == 0x00A0; }

}  // namespace

auto TextEdit::SelectedText() const -> std::u16string {
    return text_.substr(SelectionStart(), SelectionEnd() - SelectionStart());
}

void TextEdit::SetText(std::u16string_view text, bool selectAll) {
    text_.assign(text.substr(0, std::min(text.size(), maxLength_)));
    if (!text_.empty() && IsHighSurrogate(text_.back())) text_.pop_back();
    caret_ = text_.size();
    anchor_ = selectAll ? 0 : caret_;
    pendingHigh_ = 0;
}

auto TextEdit::Snap(std::size_t at) const -> std::size_t {
    at = std::min(at, text_.size());
    if (at > 0 && at < text_.size() && IsLowSurrogate(text_[at]) && IsHighSurrogate(text_[at - 1])) --at;
    return at;
}

auto TextEdit::PrevPoint(std::size_t at) const -> std::size_t {
    if (at == 0) return 0;
    --at;
    if (at > 0 && IsLowSurrogate(text_[at]) && IsHighSurrogate(text_[at - 1])) --at;
    return at;
}

auto TextEdit::NextPoint(std::size_t at) const -> std::size_t {
    if (at >= text_.size()) return text_.size();
    ++at;
    if (at < text_.size() && IsLowSurrogate(text_[at]) && IsHighSurrogate(text_[at - 1])) ++at;
    return at;
}

// Ctrl+Left: to the start of this word, or of the word before (spaces skipped first).
auto TextEdit::PrevWord(std::size_t at) const -> std::size_t {
    while (at > 0 && IsSpace(text_[at - 1])) --at;
    while (at > 0 && !IsSpace(text_[at - 1])) --at;
    return at;
}

// Ctrl+Right: past this word and the spaces after it.
auto TextEdit::NextWord(std::size_t at) const -> std::size_t {
    while (at < text_.size() && !IsSpace(text_[at])) ++at;
    while (at < text_.size() && IsSpace(text_[at])) ++at;
    return at;
}

auto TextEdit::Accepts(char16_t c, std::size_t at) const -> bool {
    if (c < 0x20 || c == 0x7F) return false;  // control characters (Backspace, Enter, Tab, Ctrl+letters)
    if (filter_ == Filter::Integer) {
        if (c >= u'0' && c <= u'9') return true;
        // a minus sign only at the start, and only once
        return c == u'-' && at == 0 && (text_.empty() || text_.front() != u'-');
    }
    return true;
}

auto TextEdit::DeleteSelection() -> bool {
    if (!HasSelection()) return false;
    const std::size_t start = SelectionStart();
    text_.erase(start, SelectionEnd() - start);
    caret_ = anchor_ = start;
    return true;
}

auto TextEdit::Insert(std::u16string_view input) -> bool {
    pendingHigh_ = 0;
    const bool removed = DeleteSelection();
    std::u16string accepted;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char16_t c = input[i];
        if (IsHighSurrogate(c)) {
            if (i + 1 < input.size() && IsLowSurrogate(input[i + 1])) {
                if (filter_ == Filter::Any) {
                    accepted += c;
                    accepted += input[i + 1];
                }
                ++i;
            }
            continue;
        }
        if (IsLowSurrogate(c)) continue;  // a lone half
        if (c == u'\r' || c == u'\n' || c == u'\t') {
            if (filter_ == Filter::Any) accepted += u' ';  // a pasted line break becomes a space: this is one line
            continue;
        }
        if (Accepts(c, caret_ + accepted.size())) accepted += c;
    }
    const std::size_t room = maxLength_ > text_.size() ? maxLength_ - text_.size() : 0;
    if (accepted.size() > room) {
        accepted.resize(room);
        if (!accepted.empty() && IsHighSurrogate(accepted.back())) accepted.pop_back();
    }
    if (accepted.empty()) return removed;
    text_.insert(caret_, accepted);
    caret_ += accepted.size();
    anchor_ = caret_;
    return true;
}

auto TextEdit::TypeChar(char16_t c) -> bool {
    if (IsHighSurrogate(c)) {
        pendingHigh_ = c;
        return false;
    }
    if (IsLowSurrogate(c)) {
        if (pendingHigh_ == 0) return false;
        const char16_t pair[2] = {pendingHigh_, c};
        pendingHigh_ = 0;
        return Insert(std::u16string_view(pair, 2));
    }
    pendingHigh_ = 0;
    if (c < 0x20 || c == 0x7F) return false;
    const char16_t one[1] = {c};
    return Insert(std::u16string_view(one, 1));
}

auto TextEdit::Backspace(bool word) -> bool {
    pendingHigh_ = 0;
    if (DeleteSelection()) return true;
    if (caret_ == 0) return false;
    const std::size_t from = word ? PrevWord(caret_) : PrevPoint(caret_);
    text_.erase(from, caret_ - from);
    caret_ = anchor_ = from;
    return true;
}

auto TextEdit::Delete(bool word) -> bool {
    pendingHigh_ = 0;
    if (DeleteSelection()) return true;
    if (caret_ >= text_.size()) return false;
    const std::size_t to = word ? NextWord(caret_) : NextPoint(caret_);
    text_.erase(caret_, to - caret_);
    anchor_ = caret_;
    return true;
}

auto TextEdit::Clear() -> bool {
    pendingHigh_ = 0;
    if (text_.empty()) return false;
    text_.clear();
    caret_ = anchor_ = 0;
    return true;
}

void TextEdit::Left(bool extend, bool word) {
    if (!extend && HasSelection() && !word) {
        caret_ = anchor_ = SelectionStart();
        return;
    }
    caret_ = word ? PrevWord(caret_) : PrevPoint(caret_);
    if (!extend) anchor_ = caret_;
}

void TextEdit::Right(bool extend, bool word) {
    if (!extend && HasSelection() && !word) {
        caret_ = anchor_ = SelectionEnd();
        return;
    }
    caret_ = word ? NextWord(caret_) : NextPoint(caret_);
    if (!extend) anchor_ = caret_;
}

void TextEdit::Home(bool extend) {
    caret_ = 0;
    if (!extend) anchor_ = caret_;
}

void TextEdit::End(bool extend) {
    caret_ = text_.size();
    if (!extend) anchor_ = caret_;
}

void TextEdit::SelectAll() {
    anchor_ = 0;
    caret_ = text_.size();
}

void TextEdit::SetCaret(std::size_t position, bool extend) {
    caret_ = Snap(position);
    if (!extend) anchor_ = caret_;
}

auto TextEdit::PositionAt(int x, const std::function<int(std::u16string_view)>& prefixWidth) const -> std::size_t {
    if (x <= 0 || text_.empty()) return 0;
    std::size_t before = 0;
    int beforeX = 0;
    for (std::size_t at = NextPoint(0);; at = NextPoint(at)) {
        const int atX = prefixWidth(std::u16string_view(text_).substr(0, at));
        if (atX >= x) return (x - beforeX) * 2 < (atX - beforeX) ? before : at;  // the nearer edge of this character
        before = at;
        beforeX = atX;
        if (at >= text_.size()) return text_.size();
    }
}

auto TextEdit::ScrollForCaret(int scroll, int width, const std::function<int(std::u16string_view)>& prefixWidth) const -> int {
    if (width <= 0) return 0;
    const int caretX = prefixWidth(std::u16string_view(text_).substr(0, caret_));
    const int total = prefixWidth(text_);
    const int margin = std::min(width / 4, 24);
    if (caretX - scroll > width - 1) scroll = caretX - width + 1 + margin;
    if (caretX - scroll < 0) scroll = caretX - margin;
    scroll = std::min(scroll, std::max(0, total - width + 1));
    return std::max(0, scroll);
}

}  // namespace esrfe::overlay
