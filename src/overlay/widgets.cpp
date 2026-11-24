// The look of each widget is the wiki overlay's (overlay.cpp): buttons and chips as its paging buttons and filter chips
// (BuildControls, PaintList), the gold button as its switch buttons (overlay_switch.inc), list rows as its entry list
// (PaintList), the scroll bar as DrawScrollbar, the text box as its search box, the drop-down and number box as its
// IAS calculator fields. The behaviour of the text box, number box and drop-down is new: the wiki used a Windows EDIT
// control for its only text box.
#include "widgets.hpp"

#include <algorithm>
#include <cstdlib>

#include "ui_text.hpp"
#include "utf.hpp"

namespace esrfe::overlay {
namespace {

using namespace text_flags;

auto Tr(const std::string& text, bool translate) -> std::string { return translate ? UiText(text) : text; }

}  // namespace

// ---- Widget defaults -----------------------------------------------------------------------------------------------

auto Widget::Mouse(const MouseEvent&, Ui&) -> bool { return false; }
auto Widget::Wheel(int, int, int, Ui&) -> bool { return false; }
auto Widget::Key(const KeyEvent&, Ui&) -> bool { return false; }
auto Widget::Char(char16_t, Ui&) -> bool { return false; }
auto Widget::Pad(const PadEvent&, Ui&) -> bool { return false; }
auto Widget::ScrollBy(int, Ui&) -> bool { return false; }
void Widget::HelpRows(std::vector<HelpRow>&) const {}
void Widget::FocusChanged(bool, Ui&) {}
void Widget::Leave(Ui&) {}
void Widget::PointFocus(int, int, Ui&) {}

auto Widget::TooltipAt(int, int, Box& anchor) const -> std::string {
    anchor = box;
    return tooltip;
}

auto Widget::FocusTooltip(Box& anchor) const -> std::string {
    anchor = FocusBox();
    return tooltip;
}

void PaintRowBackground(Canvas& canvas, const Box& row, bool selected, bool hovered) {
    const Theme& t = canvas.GetTheme();
    canvas.CornerShadow(row, canvas.S(2));
    canvas.FillAlpha(row, selected ? t.selected : t.panel, t.panelAlpha);
    if (selected) canvas.Frame(row, t.gold);
    else if (hovered) canvas.Frame(row, t.border);
}

void PaintFocusFrame(Canvas& canvas, const Box& box) {
    for (int grow : {canvas.S(1), canvas.S(2)}) canvas.Frame(box.Inflated(grow), canvas.GetTheme().focus);
}

// ---- Label, Panel --------------------------------------------------------------------------------------------------

void Label::Paint(Canvas& canvas, const PaintState&) {
    const Theme& t = canvas.GetTheme();
    const std::string shown = Tr(text, translate);
    if (heading) {
        // Stacked dark copies at growing offsets make a heavier shadow, which shows on the dark header.
        for (int offset : {canvas.S(1), canvas.S(2), canvas.S(3)})
            canvas.Text(shown, box.Offset(offset, offset), Rgb{0, 0, 0}, font, flags | kNoHighlight);
        canvas.Text(shown, box, t.headerGold, font, flags);
        return;
    }
    canvas.Text(shown, box, useThemeColor ? t.body : color, font, flags);
}

void Panel::Paint(Canvas& canvas, const PaintState&) {
    const Theme& t = canvas.GetTheme();
    canvas.CornerShadow(box, canvas.S(2));
    canvas.FillAlpha(box, t.panel, t.panelAlpha);
    if (frame) canvas.Frame(box, t.border);
    if (!title.empty()) {
        const int line = canvas.LineHeight(Font::Bold);
        canvas.Text(UiText(title), Box{box.left + canvas.S(12), box.top + canvas.S(8), box.right - canvas.S(12), box.top + canvas.S(8) + line},
                    t.headerGold, Font::Bold, kSingleLine | kEllipsis);
    }
}

// ---- Button, Chip --------------------------------------------------------------------------------------------------

void Button::Paint(Canvas& canvas, const PaintState& state) {
    const Theme& t = canvas.GetTheme();
    const std::string shown = Tr(label, translate);
    if (style == Style::Primary) {
        // Like the launcher's gold PLAY button: a gold fill with dark lettering.
        canvas.DropShadow(box, canvas.S(2));
        canvas.FillAlpha(box, t.primaryFill, enabled ? t.keyAlpha : 120);
        canvas.Frame(box, t.primaryLight);
        canvas.Frame(box.Inflated(-1), t.primaryDark);
        canvas.Text(shown, box, t.primaryText, Font::Bold, kCenter | kVCenter | kSingleLine);
        return;
    }
    canvas.CornerShadow(box, canvas.S(2));
    canvas.FillAlpha(box, state.pressed ? t.selected : t.panel, t.controlAlpha);
    const bool danger = style == Style::Danger;
    canvas.Frame(box, danger ? t.danger : (state.hovered && enabled ? t.gold : t.border));
    const Rgb color = !enabled ? t.dim : (danger ? t.danger : t.text);
    canvas.Text(shown, box, color, font, kCenter | kVCenter | kSingleLine);
}

void Button::Activate(Ui& ui) {
    if (!enabled) return;
    if (onClick) onClick(ui);
    ui.Repaint();
}

// The wiki acts on the press (WM_LBUTTONDOWN), not the release; so does this.
auto Button::Mouse(const MouseEvent& event, Ui& ui) -> bool {
    if (event.kind != MouseKind::Down) return event.kind == MouseKind::Move;
    Activate(ui);
    return true;
}

auto Button::Key(const KeyEvent& event, Ui& ui) -> bool {
    if (event.key != Key::Enter && event.key != Key::Space) return false;
    if (!event.repeat) Activate(ui);
    return true;
}

auto Button::Pad(const PadEvent& event, Ui& ui) -> bool {
    if (event.button != pad::Button::A) return false;
    Activate(ui);
    return true;
}

void Button::HelpRows(std::vector<HelpRow>& out) const { out.push_back({Glyph::A, "Enter", "Select"}); }

void Chip::Paint(Canvas& canvas, const PaintState& state) {
    const Theme& t = canvas.GetTheme();
    canvas.CornerShadow(box, canvas.S(2));
    canvas.FillAlpha(box, selected ? t.selected : t.panel, t.controlAlpha);
    canvas.Frame(box, selected || (state.hovered && enabled) ? t.gold : t.border);
    canvas.Text(Tr(label, translate), box, !enabled ? t.dim : (selected ? t.gold : t.text), font, kCenter | kVCenter | kSingleLine);
}

// ---- Toggle, Checkbox ----------------------------------------------------------------------------------------------

void Toggle::Flip(Ui& ui) {
    if (!enabled) return;
    on = !on;
    if (onChange) onChange(on, ui);
    ui.Repaint();
}

void Toggle::Paint(Canvas& canvas, const PaintState& state) {
    const Theme& t = canvas.GetTheme();
    const std::string onText = UiText("On"), offText = UiText("Off");
    const int pillWidth = std::max(canvas.S(56), std::max(canvas.TextWidth(onText, Font::Small), canvas.TextWidth(offText, Font::Small)) + canvas.S(20));
    const Box pill{box.right - pillWidth, box.top + canvas.S(2), box.right, box.bottom - canvas.S(2)};
    canvas.Text(Tr(label, translate), Box{box.left, box.top, pill.left - canvas.S(8), box.bottom}, enabled ? t.text : t.dim, Font::Body,
                kVCenter | kSingleLine | kEllipsis);
    canvas.CornerShadow(pill, canvas.S(2));
    canvas.FillAlpha(pill, on ? t.selected : t.panel, t.controlAlpha);
    canvas.Frame(pill, on || (state.hovered && enabled) ? t.gold : t.border);
    canvas.Text(on ? onText : offText, pill, !enabled ? t.dim : (on ? t.gold : t.dim), Font::Small, kCenter | kVCenter | kSingleLine);
}

auto Toggle::Mouse(const MouseEvent& event, Ui& ui) -> bool {
    if (event.kind != MouseKind::Down) return event.kind == MouseKind::Move;
    Flip(ui);
    return true;
}

auto Toggle::Key(const KeyEvent& event, Ui& ui) -> bool {
    if (event.key != Key::Enter && event.key != Key::Space) return false;
    if (!event.repeat) Flip(ui);
    return true;
}

auto Toggle::Pad(const PadEvent& event, Ui& ui) -> bool {
    if (event.button != pad::Button::A) return false;
    Flip(ui);
    return true;
}

void Toggle::HelpRows(std::vector<HelpRow>& out) const { out.push_back({Glyph::A, "Space", "Switch on / off"}); }

void Checkbox::Paint(Canvas& canvas, const PaintState& state) {
    const Theme& t = canvas.GetTheme();
    const int size = std::min(box.Height(), canvas.S(18));
    const Box tick{box.left, box.CentreY() - size / 2, box.left + size, box.CentreY() - size / 2 + size};
    canvas.CornerShadow(tick, canvas.S(2));
    canvas.Fill(tick, t.field);
    canvas.Frame(tick, (state.hovered || state.focused) && enabled ? t.gold : t.border);
    if (on) canvas.Fill(tick.Inflated(-canvas.S(4)), enabled ? t.gold : t.dim);
    canvas.Text(Tr(label, translate), Box{tick.right + canvas.S(8), box.top, box.right, box.bottom}, enabled ? t.text : t.dim, Font::Body,
                kVCenter | kSingleLine | kEllipsis);
}

// ---- TextBox -------------------------------------------------------------------------------------------------------

auto TextBox::Value() const -> std::string { return Utf16ToUtf8(edit.Text()); }

void TextBox::SetValue(const std::string& utf8) {
    edit.SetText(Utf8ToUtf16(utf8));
    scroll_ = 0;
}

void TextBox::Changed(Ui& ui) {
    if (onChange) onChange(Value(), ui);
    ui.Repaint();
}

auto TextBox::ClearBox() const -> Box {
    if (!search || edit.Text().empty()) return Box{};
    return Box{box.right - box.Height(), box.top, box.right, box.bottom};
}

auto TextBox::TextArea() const -> Box {
    const int pad = std::max(4, box.Height() / 5);
    const Box clear = ClearBox();
    return Box{box.left + pad, box.top + 1, (clear.Empty() ? box.right : clear.left) - pad, box.bottom - 1};
}

auto TextBox::PositionAtX(int x, Measurer& measure) const -> std::size_t {
    const Box area = TextArea();
    return edit.PositionAt(x - area.left + scroll_, [&](std::u16string_view prefix) { return measure.TextWidth16(prefix, Font::Body); });
}

auto TextBox::CaretBox(Measurer& measure) const -> Box {
    const Box area = TextArea();
    const int line = measure.LineHeight(Font::Body);
    const int x = area.left - scroll_ + measure.TextWidth16(std::u16string_view(edit.Text()).substr(0, edit.Caret()), Font::Body);
    const int top = area.CentreY() - line / 2;
    return Box{x, top, x + 1, top + line};
}

void TextBox::Paint(Canvas& canvas, const PaintState& state) {
    const Theme& t = canvas.GetTheme();
    canvas.CornerShadow(box, canvas.S(2));
    canvas.Fill(box, t.field);
    canvas.Frame(box, state.focused ? t.gold : t.border);
    const Box area = TextArea();
    const auto width = [&](std::u16string_view s) { return canvas.TextWidth16(s, Font::Body); };
    scroll_ = edit.ScrollForCaret(scroll_, area.Width(), width);
    canvas.PushClip(area);
    if (edit.Text().empty()) {
        // The grey hint in the empty box (the wiki draws it itself: EM_SETCUEBANNER needs common controls 6).
        if (!placeholder.empty())
            canvas.Text(UiText(placeholder), Box{area.left + 2, area.top, area.right, area.bottom}, t.placeholder, Font::Body,
                        kVCenter | kSingleLine | kEllipsis | kNoHighlight);
    } else {
        const std::u16string_view text(edit.Text());
        if (state.focused && edit.HasSelection()) {
            const int from = area.left - scroll_ + width(text.substr(0, edit.SelectionStart()));
            const int to = area.left - scroll_ + width(text.substr(0, edit.SelectionEnd()));
            const int line = canvas.LineHeight(Font::Body);
            canvas.Fill(Box{from, area.CentreY() - line / 2, to, area.CentreY() - line / 2 + line}, t.highlight);
        }
        canvas.Text16(text, Box{area.left - scroll_, area.top, area.right + 100000, area.bottom}, enabled ? t.text : t.dim, Font::Body,
                      kVCenter | kSingleLine);
    }
    if (state.focused) {
        const Box caret = CaretBox(canvas);
        canvas.Fill(Box{caret.left, caret.top, caret.left + std::max(1, canvas.S(1)), caret.bottom}, t.text);
    }
    canvas.PopClip();
    const Box clear = ClearBox();
    if (!clear.Empty()) {
        canvas.CornerShadow(clear, canvas.S(2));
        canvas.FillAlpha(clear, t.panel, t.controlAlpha);
        canvas.Frame(clear, t.border);
        canvas.Text("X", clear, t.text, Font::Body, kCenter | kVCenter | kSingleLine | kRaw);
    }
}

auto TextBox::Mouse(const MouseEvent& event, Ui& ui) -> bool {
    Measurer& measure = ui.GetMeasurer();
    switch (event.kind) {
    case MouseKind::Down: {
        const Box clear = ClearBox();
        if (!clear.Empty() && clear.Contains(event.x, event.y)) {
            if (edit.Clear()) Changed(ui);
            return true;
        }
        if (ui.PadActive()) {  // a click on the box with the controller's cursor opens the on-screen keyboard
            ui.OpenKeyboard(*this);
            return true;
        }
        if (event.clicks >= 2) {
            edit.SelectAll();
        } else {
            edit.SetCaret(PositionAtX(event.x, measure), event.mods.shift);
            selecting_ = true;
            ui.Capture(this);
        }
        ui.Repaint();
        return true;
    }
    case MouseKind::Move:
        if (selecting_) {
            edit.SetCaret(PositionAtX(event.x, measure), true);
            ui.Repaint();
        }
        return true;
    case MouseKind::Up:
        if (selecting_) {
            selecting_ = false;
            ui.Capture(nullptr);
        }
        return true;
    default:
        return false;
    }
}

auto TextBox::Key(const KeyEvent& event, Ui& ui) -> bool {
    const bool shift = event.mods.shift, ctrl = event.mods.ctrl;
    bool changed = false;
    switch (event.key) {
    case Key::Left: edit.Left(shift, ctrl); break;
    case Key::Right: edit.Right(shift, ctrl); break;
    case Key::Home: edit.Home(shift); break;
    case Key::End: edit.End(shift); break;
    case Key::Backspace: changed = edit.Backspace(ctrl); break;
    case Key::Delete:
        if (shift && edit.HasSelection()) {  // Shift+Delete cuts
            ui.SetClipboardText(Utf16ToUtf8(edit.SelectedText()));
        }
        changed = edit.Delete(ctrl);
        break;
    case Key::Insert:
        if (ctrl && edit.HasSelection()) ui.SetClipboardText(Utf16ToUtf8(edit.SelectedText()));
        else if (shift) changed = edit.Insert(Utf8ToUtf16(ui.ClipboardText()));
        else return false;
        break;
    case Key::A:
        if (!ctrl) return false;
        edit.SelectAll();
        break;
    case Key::C:
        if (!ctrl) return false;
        if (edit.HasSelection()) ui.SetClipboardText(Utf16ToUtf8(edit.SelectedText()));
        break;
    case Key::X:
        if (!ctrl) return false;
        if (edit.HasSelection()) {
            ui.SetClipboardText(Utf16ToUtf8(edit.SelectedText()));
            changed = edit.DeleteSelection();
        }
        break;
    case Key::V:
        if (!ctrl) return false;
        changed = edit.Insert(Utf8ToUtf16(ui.ClipboardText()));
        break;
    case Key::Enter:
        if (!onSubmit) return false;
        if (!event.repeat) onSubmit(Value(), ui);
        break;
    default:
        return false;
    }
    if (changed) Changed(ui);
    else ui.Repaint();
    return true;
}

auto TextBox::Char(char16_t c, Ui& ui) -> bool {
    if (c < 0x20 || c == 0x7F) return false;  // Backspace, Enter, Tab and Ctrl+letters come as keys
    if (edit.TypeChar(c)) Changed(ui);
    return true;
}

auto TextBox::Pad(const PadEvent& event, Ui& ui) -> bool {
    switch (event.button) {
    case pad::Button::A:
        ui.OpenKeyboard(*this);
        return true;
    case pad::Button::Y:
        if (ui.KeyboardOpen()) ui.CloseKeyboard();
        else ui.OpenKeyboard(*this);
        return true;
    case pad::Button::X:
        if (!search) return false;
        if (edit.Clear()) Changed(ui);  // the wiki's X: clear the search
        return true;
    default:
        return false;
    }
}

void TextBox::HelpRows(std::vector<HelpRow>& out) const {
    out.push_back({Glyph::A, "", "Type (on-screen keyboard)"});
    out.push_back({Glyph::Y, "", "Show / hide keyboard"});
    if (search) out.push_back({Glyph::X, "", "Clear search"});
    if (onSubmit) out.push_back({Glyph::None, "Enter", "Done"});
    out.push_back({Glyph::None, "Ctrl+V", "Paste"});
}

void TextBox::FocusChanged(bool focused, Ui& ui) {
    if (!focused) {
        selecting_ = false;
        edit.SetCaret(edit.Caret());  // the selection goes with the focus
        if (ui.KeyboardOpen()) ui.CloseKeyboard();
    }
}

// ---- NumberBox -----------------------------------------------------------------------------------------------------

void NumberBox::SetValue(int v) { value = std::clamp(v, minimum, std::max(minimum, maximum)); }

auto NumberBox::MinusBox() const -> Box { return Box{box.left, box.top, box.left + box.Height(), box.bottom}; }
auto NumberBox::PlusBox() const -> Box { return Box{box.right - box.Height(), box.top, box.right, box.bottom}; }

void NumberBox::StepBy(int steps, Ui& ui) {
    const int before = value;
    const long long next = static_cast<long long>(value) + static_cast<long long>(steps) * std::max(1, step);
    SetValue(static_cast<int>(std::clamp<long long>(next, minimum, std::max(minimum, maximum))));
    if (value != before && onChange) onChange(value, ui);
    ui.Repaint();
}

void NumberBox::Commit(Ui& ui) {
    if (!typing_) return;
    typing_ = false;
    const std::string text = Utf16ToUtf8(edit_.Text());
    if (text.empty() || text == "-") {
        ui.Repaint();
        return;  // nothing typed: the value stays
    }
    const long long typed = std::strtoll(text.c_str(), nullptr, 10);
    const int before = value;
    SetValue(static_cast<int>(std::clamp<long long>(typed, minimum, std::max(minimum, maximum))));
    if (value != before && onChange) onChange(value, ui);
    ui.Repaint();
}

void NumberBox::Paint(Canvas& canvas, const PaintState& state) {
    const Theme& t = canvas.GetTheme();
    const Box minus = MinusBox(), plus = PlusBox();
    const Box middle{minus.right + canvas.S(4), box.top, plus.left - canvas.S(4), box.bottom};
    for (const Box& b : {minus, plus}) {
        canvas.CornerShadow(b, canvas.S(2));
        canvas.FillAlpha(b, t.panel, t.controlAlpha);
        canvas.Frame(b, t.border);
    }
    const bool canDown = enabled && value > minimum, canUp = enabled && value < maximum;
    canvas.Text("-", minus, canDown ? t.text : t.dim, Font::Bold, kCenter | kVCenter | kSingleLine | kRaw);
    canvas.Text("+", plus, canUp ? t.text : t.dim, Font::Bold, kCenter | kVCenter | kSingleLine | kRaw);
    canvas.CornerShadow(middle, canvas.S(2));
    canvas.Fill(middle, t.field);
    canvas.Frame(middle, state.focused ? t.gold : t.border);
    if (typing_) {
        const std::u16string_view text(edit_.Text());
        const int w = canvas.TextWidth16(text, Font::Body);
        const int x = middle.CentreX() - w / 2;
        if (edit_.HasSelection())
            canvas.Fill(Box{x, middle.top + canvas.S(4), x + w, middle.bottom - canvas.S(4)}, t.highlight);
        canvas.Text16(text, Box{x, middle.top, x + w + 1, middle.bottom}, t.text, Font::Body, kVCenter | kSingleLine);
        const int caretX = x + canvas.TextWidth16(text.substr(0, edit_.Caret()), Font::Body);
        canvas.Fill(Box{caretX, middle.top + canvas.S(5), caretX + std::max(1, canvas.S(1)), middle.bottom - canvas.S(5)}, t.text);
    } else {
        canvas.Text(std::to_string(value), middle, enabled ? t.text : t.dim, Font::Body, kCenter | kVCenter | kSingleLine | kRaw);
    }
}

auto NumberBox::Mouse(const MouseEvent& event, Ui& ui) -> bool {
    if (event.kind != MouseKind::Down) return event.kind == MouseKind::Move;
    if (MinusBox().Contains(event.x, event.y)) {
        Commit(ui);
        StepBy(-1, ui);
    } else if (PlusBox().Contains(event.x, event.y)) {
        Commit(ui);
        StepBy(1, ui);
    } else if (!ui.PadActive()) {  // the middle: type a new value
        typing_ = true;
        edit_.SetFilter(TextEdit::Filter::Integer);
        edit_.SetMaxLength(11);
        edit_.SetText(Utf8ToUtf16(std::to_string(value)), true);
        ui.Repaint();
    }
    return true;
}

auto NumberBox::Wheel(int steps, int, int, Ui& ui) -> bool {
    if (!enabled) return false;
    Commit(ui);
    StepBy(steps, ui);
    return true;
}

auto NumberBox::Key(const KeyEvent& event, Ui& ui) -> bool {
    if (typing_) {
        switch (event.key) {
        case Key::Enter: Commit(ui); return true;
        case Key::Escape:
            typing_ = false;  // back to the value it had
            ui.Repaint();
            return true;
        case Key::Backspace: edit_.Backspace(); break;
        case Key::Delete: edit_.Delete(); break;
        case Key::Left: edit_.Left(event.mods.shift); break;
        case Key::Right: edit_.Right(event.mods.shift); break;
        case Key::Home: edit_.Home(event.mods.shift); break;
        case Key::End: edit_.End(event.mods.shift); break;
        case Key::Up:
        case Key::Down:
        case Key::Tab:
            Commit(ui);
            return false;  // the focus moves on, with the value kept
        default: return false;
        }
        ui.Repaint();
        return true;
    }
    switch (event.key) {
    case Key::Left:
    case Key::Right: {
        holdRepeats_ = event.repeat ? holdRepeats_ + 1 : 0;
        StepBy((event.key == Key::Left ? -1 : 1) * pad::NumberStep(holdRepeats_), ui);
        return true;
    }
    case Key::PageUp: StepBy(10, ui); return true;
    case Key::PageDown: StepBy(-10, ui); return true;
    case Key::Home: SetValue(minimum); if (onChange) onChange(value, ui); ui.Repaint(); return true;
    case Key::End: SetValue(maximum); if (onChange) onChange(value, ui); ui.Repaint(); return true;
    default: return false;
    }
}

auto NumberBox::Char(char16_t c, Ui& ui) -> bool {
    if (!((c >= u'0' && c <= u'9') || c == u'-')) return c >= 0x20;
    if (!typing_) {
        typing_ = true;
        edit_.SetFilter(TextEdit::Filter::Integer);
        edit_.SetMaxLength(11);
        edit_.SetText(u"");
    }
    edit_.TypeChar(c);
    ui.Repaint();
    return true;
}

auto NumberBox::Pad(const PadEvent& event, Ui& ui) -> bool {
    switch (event.button) {
    case pad::Button::Left:
    case pad::Button::Right: {
        Commit(ui);
        // 1 at first, then 5, then 10 while the D-pad is held (the wiki calculator's rule)
        holdRepeats_ = event.repeat ? holdRepeats_ + 1 : 0;
        StepBy((event.button == pad::Button::Left ? -1 : 1) * pad::NumberStep(holdRepeats_), ui);
        return true;
    }
    case pad::Button::X: {  // the wiki's X on a number: 0 (or the nearest allowed value)
        const int before = value;
        SetValue(0);
        if (value != before && onChange) onChange(value, ui);
        ui.Repaint();
        return true;
    }
    default:
        return false;
    }
}

void NumberBox::HelpRows(std::vector<HelpRow>& out) const {
    out.push_back({Glyph::DPadLeftRight, "Left / Right", "Change the number (hold: faster)"});
    out.push_back({Glyph::X, "", "Set to 0"});
    out.push_back({Glyph::None, "0-9", "Type a number"});
}

void NumberBox::FocusChanged(bool focused, Ui& ui) {
    if (!focused) Commit(ui);
}

// ---- Dropdown ------------------------------------------------------------------------------------------------------

auto Dropdown::SelectedText() const -> std::string {
    if (selected < 0 || selected >= static_cast<int>(options.size())) return {};
    return Tr(options[static_cast<std::size_t>(selected)], translate);
}

void Dropdown::Paint(Canvas& canvas, const PaintState& state) {
    const Theme& t = canvas.GetTheme();
    canvas.CornerShadow(box, canvas.S(2));
    canvas.Fill(box, t.field);
    canvas.Frame(box, state.focused || (state.hovered && enabled) ? t.gold : t.border);
    const int arrow = box.Height();
    canvas.Text(SelectedText(), Box{box.left + canvas.S(8), box.top, box.right - arrow, box.bottom}, enabled ? t.text : t.dim, Font::Body,
                kVCenter | kSingleLine | kEllipsis);
    canvas.Text("\xE2\x96\xBC", Box{box.right - arrow, box.top, box.right, box.bottom}, enabled ? t.gold : t.dim, Font::Small,
                kCenter | kVCenter | kSingleLine | kRaw);  // ▼
}

auto Dropdown::Mouse(const MouseEvent& event, Ui& ui) -> bool {
    if (event.kind != MouseKind::Down) return event.kind == MouseKind::Move;
    if (enabled && !options.empty()) ui.OpenChoices(*this);
    return true;
}

auto Dropdown::Wheel(int, int, int, Ui&) -> bool { return false; }

auto Dropdown::Key(const KeyEvent& event, Ui& ui) -> bool {
    const bool opens = event.key == Key::Enter || event.key == Key::Space || (event.key == Key::Down && event.mods.alt);
    if (!opens) return false;
    if (enabled && !options.empty() && !event.repeat) ui.OpenChoices(*this);
    return true;
}

auto Dropdown::Pad(const PadEvent& event, Ui& ui) -> bool {
    if (event.button != pad::Button::A) return false;
    if (enabled && !options.empty()) ui.OpenChoices(*this);
    return true;
}

void Dropdown::HelpRows(std::vector<HelpRow>& out) const { out.push_back({Glyph::A, "Enter", "Open the choices"}); }

void Dropdown::PopupOpened(const Box& client, Measurer& measure) {
    const int count = static_cast<int>(options.size());
    highlight_ = selected >= 0 && selected < count ? selected : (count > 0 ? 0 : -1);
    PopupPlace(client, measure);
    popupScroll_ = std::clamp(highlight_ - visibleRows_ / 2, 0, std::max(0, count - visibleRows_));
}

// Under the drop-down (over it when there is more room there), as wide as its longest choice, inside the window.
void Dropdown::PopupPlace(const Box& client, Measurer& measure) {
    const int edge = measure.S(8);
    rowHeight_ = std::max(measure.S(26), measure.LineHeight(Font::Body) + measure.S(8));
    const int count = static_cast<int>(options.size());
    int widest = box.Width();
    for (const std::string& option : options) widest = std::max(widest, measure.TextWidth(Tr(option, translate), Font::Body) + measure.S(40));
    const int width = std::min(widest, client.Width() - 2 * edge);
    visibleRows_ = std::clamp(count, 1, std::max(1, maxRows));
    const int pad = measure.S(4);
    const auto height = [&](int rows) { return rows * rowHeight_ + 2 * pad; };
    int top = box.bottom + measure.S(2);
    const int roomBelow = client.bottom - edge - top;
    const int roomAbove = box.top - measure.S(2) - (client.top + edge);
    if (height(visibleRows_) > roomBelow && roomAbove > roomBelow) {
        while (visibleRows_ > 1 && height(visibleRows_) > roomAbove) --visibleRows_;
        top = box.top - measure.S(2) - height(visibleRows_);
    } else {
        while (visibleRows_ > 1 && height(visibleRows_) > roomBelow) --visibleRows_;
    }
    const int left = std::clamp(box.left, client.left + edge, std::max(client.left + edge, client.right - edge - width));
    popupBox_ = Box{left, top, left + width, top + height(visibleRows_)};
    popupScroll_ = std::clamp(popupScroll_, 0, std::max(0, count - visibleRows_));
    if (highlight_ >= 0 && highlight_ < popupScroll_) popupScroll_ = highlight_;
    if (highlight_ >= popupScroll_ + visibleRows_) popupScroll_ = highlight_ - visibleRows_ + 1;
}

auto Dropdown::RowBox(int index) const -> Box {
    const int pad = (popupBox_.Height() - visibleRows_ * rowHeight_) / 2;
    const int top = popupBox_.top + pad + (index - popupScroll_) * rowHeight_;
    return Box{popupBox_.left + pad, top, popupBox_.right - pad, top + rowHeight_};
}

void Dropdown::PaintPopup(Canvas& canvas) {
    const Theme& t = canvas.GetTheme();
    canvas.DropShadow(popupBox_, canvas.S(3));
    canvas.FillAlpha(popupBox_, t.tooltipFill, t.popupAlpha);
    canvas.Frame(popupBox_, t.gold);
    const int count = static_cast<int>(options.size());
    const bool scrolls = count > visibleRows_;
    const int bar = scrolls ? canvas.S(8) : 0;
    for (int i = popupScroll_; i < std::min(count, popupScroll_ + visibleRows_); ++i) {
        Box row = RowBox(i);
        row.right -= bar;
        if (i == highlight_) {
            canvas.FillAlpha(row, t.selected, t.keyAlpha);
            canvas.Frame(row, t.gold);
        }
        canvas.Text(Tr(options[static_cast<std::size_t>(i)], translate), Box{row.left + canvas.S(8), row.top, row.right - canvas.S(4), row.bottom},
                    i == selected ? t.gold : t.text, Font::Body, kVCenter | kSingleLine | kEllipsis);
    }
    if (scrolls) {
        const Box track{popupBox_.right - bar - canvas.S(2), popupBox_.top + canvas.S(4), popupBox_.right - canvas.S(4), popupBox_.bottom - canvas.S(4)};
        canvas.FillAlpha(track, t.field, t.controlAlpha);
        const Thumb thumb = ThumbFor(track.top, track.Height(), visibleRows_, count, popupScroll_, canvas.S(16));
        if (thumb.shown) canvas.Fill(Box{track.left, thumb.top, track.right, thumb.top + thumb.height}, t.border);
    }
}

void Dropdown::MoveHighlight(int delta) {
    const int count = static_cast<int>(options.size());
    if (count == 0) return;
    highlight_ = std::clamp((highlight_ < 0 ? 0 : highlight_) + delta, 0, count - 1);
    if (highlight_ < popupScroll_) popupScroll_ = highlight_;
    if (highlight_ >= popupScroll_ + visibleRows_) popupScroll_ = highlight_ - visibleRows_ + 1;
}

void Dropdown::Choose(int index, Ui& ui) {
    ui.CloseChoices();
    if (index >= 0 && index < static_cast<int>(options.size()) && index != selected) {
        selected = index;
        if (onChange) onChange(selected, ui);
    }
    ui.Repaint();
}

auto Dropdown::PopupMouse(const MouseEvent& event, Ui& ui) -> bool {
    const bool inside = popupBox_.Contains(event.x, event.y);
    const int count = static_cast<int>(options.size());
    const auto rowAt = [&]() -> int {
        for (int i = popupScroll_; i < std::min(count, popupScroll_ + visibleRows_); ++i)
            if (RowBox(i).Contains(event.x, event.y)) return i;
        return -1;
    };
    switch (event.kind) {
    case MouseKind::Move: {
        const int row = inside ? rowAt() : -1;
        if (row >= 0 && row != highlight_) {
            highlight_ = row;
            ui.Repaint();
        }
        return true;
    }
    case MouseKind::Down: {
        if (!inside) return false;
        const int row = rowAt();
        if (row >= 0) Choose(row, ui);
        return true;
    }
    default:
        return inside;
    }
}

auto Dropdown::PopupWheel(int steps, Ui& ui) -> bool {
    const int count = static_cast<int>(options.size());
    popupScroll_ = std::clamp(popupScroll_ - steps, 0, std::max(0, count - visibleRows_));
    highlight_ = std::clamp(highlight_, popupScroll_, std::max(popupScroll_, popupScroll_ + visibleRows_ - 1));
    ui.Repaint();
    return true;
}

auto Dropdown::PopupKey(const KeyEvent& event, Ui& ui) -> bool {
    switch (event.key) {
    case Key::Up: MoveHighlight(-1); break;
    case Key::Down: MoveHighlight(1); break;
    case Key::PageUp: MoveHighlight(-visibleRows_); break;
    case Key::PageDown: MoveHighlight(visibleRows_); break;
    case Key::Home: MoveHighlight(-static_cast<int>(options.size())); break;
    case Key::End: MoveHighlight(static_cast<int>(options.size())); break;
    case Key::Enter:
    case Key::Space:
        if (!event.repeat) Choose(highlight_, ui);
        return true;
    case Key::Escape:
    case Key::Tab:
        ui.CloseChoices();
        ui.Repaint();
        return true;
    default:
        return true;  // the list is modal: other keys do nothing
    }
    ui.Repaint();
    return true;
}

auto Dropdown::PopupPad(const PadEvent& event, Ui& ui) -> bool {
    switch (event.button) {
    case pad::Button::Up: MoveHighlight(-1); break;
    case pad::Button::Down: MoveHighlight(1); break;
    case pad::Button::LB: MoveHighlight(-visibleRows_); break;
    case pad::Button::RB: MoveHighlight(visibleRows_); break;
    case pad::Button::A: Choose(highlight_, ui); return true;
    case pad::Button::B:
        ui.CloseChoices();
        ui.Repaint();
        return true;
    default:
        return true;
    }
    ui.Repaint();
    return true;
}

// ---- ListView ------------------------------------------------------------------------------------------------------

namespace {
constexpr int kBarColumn = 18;  // at 96 dpi: the wiki's scroll bar sits 8..16 px in from the pane's right edge
}

auto ListView::ViewBox() const -> Box {
    const int viewport = box.Height();
    const long long content = static_cast<long long>(count) * Pitch() - rowGap;
    if (content <= viewport) return box;
    return Box{box.left, box.top, box.right - barColumn_, box.bottom};
}

auto ListView::TrackBox() const -> Box {
    // 8 px wide, 8 px in from the right edge (at 96 dpi), as the wiki's detail pane scroll bar
    return Box{box.right - barColumn_ * 16 / kBarColumn, box.top, box.right - barColumn_ * 8 / kBarColumn, box.bottom};
}

auto ListView::ThumbNow() const -> Thumb {
    const Box track = TrackBox();
    const long long total = static_cast<long long>(count) * Pitch() - rowGap;
    return ThumbFor(track.top, track.Height(), box.Height(), total, scroll_, minThumb_);
}

auto ListView::PageRows() const -> int { return std::max(1, box.Height() / std::max(1, Pitch())); }

auto ListView::Visible() const -> RowRange { return VisibleRows(count, Pitch(), scroll_, box.Height()); }

void ListView::SetScroll(int pixels) { scroll_ = ClampScroll(pixels, count, Pitch(), box.Height() + rowGap); }

void ListView::SetCount(int rows) {
    count = std::max(0, rows);
    if (selected_ >= count) selected_ = count - 1;
    hover_ = -1;
    SetScroll(scroll_);
}

void ListView::Select(int index, Ui* ui) {
    if (count <= 0) {
        selected_ = -1;
        return;
    }
    index = std::clamp(index, 0, count - 1);
    const bool moved = index != selected_;
    selected_ = index;
    scroll_ = ScrollToShow(scroll_, index, count, Pitch(), box.Height() + rowGap);
    if (ui != nullptr) {
        if (moved && onSelect) onSelect(selected_, *ui);
        ui->Repaint();
    }
}

auto ListView::RowBox(int index) const -> Box {
    const Box view = ViewBox();
    const int top = view.top + index * Pitch() - scroll_;
    return Box{view.left, top, view.right, top + rowHeight};
}

auto ListView::FocusBox() const -> Box {
    if (selected_ >= 0 && selected_ < count) {
        const Box row = RowBox(selected_);
        if (row.bottom > box.top && row.top < box.bottom) return row.Intersect(box);
    }
    return box;
}

void ListView::Paint(Canvas& canvas, const PaintState& state) {
    const Theme& t = canvas.GetTheme();
    barColumn_ = canvas.S(kBarColumn);
    minThumb_ = canvas.S(28);
    if (count == 0) {
        canvas.Text(UiText(emptyText), Box{box.left, box.top, box.right, box.top + std::max(canvas.S(24), canvas.LineHeight(Font::Body) + canvas.S(6))},
                    t.dim, Font::Body, text_flags::kVCenter | text_flags::kSingleLine);
        return;
    }
    SetScroll(scroll_);
    const Box view = ViewBox();
    canvas.PushClip(view);
    const RowRange rows = Visible();
    for (int i = rows.first; i < rows.last; ++i) {
        const Box row = RowBox(i);
        RowState rowState{i == selected_, i == hover_, state.focused};
        if (drawRowBackground) PaintRowBackground(canvas, row, rowState.selected, rowState.hovered);
        if (paintRow) paintRow(canvas, i, row, rowState);
    }
    canvas.PopClip();
    if (view.right != box.right) {  // the scroll bar (DrawScrollbar)
        const Box track = TrackBox();
        const Thumb thumb = ThumbNow();
        canvas.CornerShadow(track, canvas.S(2));
        canvas.FillAlpha(track, t.field, t.controlAlpha);
        if (thumb.shown) canvas.Fill(Box{track.left, thumb.top, track.right, thumb.top + thumb.height}, dragging_ ? t.gold : t.border);
    }
}

auto ListView::Mouse(const MouseEvent& event, Ui& ui) -> bool {
    const Box view = ViewBox();
    const bool scrollable = view.right != box.right;
    switch (event.kind) {
    case MouseKind::Down: {
        if (scrollable && event.x >= view.right) {
            const Thumb thumb = ThumbNow();
            if (thumb.shown && event.y >= thumb.top && event.y < thumb.top + thumb.height) {
                dragging_ = true;
                dragStartY_ = event.y;
                dragStartScroll_ = scroll_;
                ui.Capture(this);
            } else {  // the track: a page up or down
                SetScroll(scroll_ + (event.y < thumb.top ? -box.Height() : box.Height()));
            }
            ui.Repaint();
            return true;
        }
        const int row = RowAt(event.y - view.top, count, Pitch(), scroll_);
        if (row >= 0 && RowBox(row).Contains(event.x, event.y)) {
            Select(row, &ui);
            if (event.clicks >= 2 && onActivate) onActivate(row, ui);
            else if (event.clicks < 2 && onClick) onClick(row, ui);
        }
        return true;
    }
    case MouseKind::Move: {
        if (dragging_) {
            const Box track = TrackBox();
            const Thumb thumb = ThumbNow();
            const long long total = static_cast<long long>(count) * Pitch() - rowGap;
            scroll_ = ScrollFromDrag(dragStartScroll_, event.y - dragStartY_, track.Height(), thumb.height, total, box.Height());
            ui.Repaint();
            return true;
        }
        int row = view.Contains(event.x, event.y) ? RowAt(event.y - view.top, count, Pitch(), scroll_) : -1;
        if (row >= 0 && !RowBox(row).Contains(event.x, event.y)) row = -1;  // in the gap between rows
        if (row != hover_) {
            hover_ = row;
            ui.Repaint();
        }
        return true;
    }
    case MouseKind::Up:
        if (dragging_) {
            dragging_ = false;
            ui.Capture(nullptr);
            ui.Repaint();
        }
        return true;
    default:
        return false;
    }
}

void ListView::PointFocus(int x, int y, Ui& ui) {
    const Box view = ViewBox();
    if (!view.Contains(x, y)) return;
    const int row = RowAt(y - view.top, count, Pitch(), scroll_);
    if (row >= 0) Select(row, &ui);
}

// The controller's focus arriving on a list with nothing selected selects its first row showing, so A has a row.
void ListView::FocusChanged(bool focused, Ui& ui) {
    if (focused && ui.PadActive() && selected_ < 0 && count > 0) Select(Visible().first, &ui);
}

void ListView::Leave(Ui& ui) {
    if (hover_ < 0) return;
    hover_ = -1;
    ui.Repaint();
}

auto ListView::Wheel(int steps, int, int, Ui& ui) -> bool {
    ScrollBy(-steps * 3 * Pitch(), ui);  // three rows a notch; the wheel never leaks past a list
    return true;
}

auto ListView::ScrollBy(int pixels, Ui& ui) -> bool {
    const int before = scroll_;
    SetScroll(scroll_ + pixels);
    if (scroll_ == before) return false;
    hover_ = -1;  // the row under a still pointer changed
    ui.Repaint();
    return true;
}

void ListView::Move(int delta, Ui& ui) {
    if (count <= 0) return;
    Select(selected_ < 0 ? (delta > 0 ? Visible().first : 0) : selected_ + delta, &ui);
}

auto ListView::Key(const KeyEvent& event, Ui& ui) -> bool {
    if (count <= 0) return false;
    switch (event.key) {
    case Key::Up:
        if (selected_ <= 0) return event.repeat;  // up from the first row: the focus moves on (not on a held key)
        Move(-1, ui);
        return true;
    case Key::Down: Move(1, ui); return true;
    case Key::PageUp: Move(-PageRows(), ui); return true;
    case Key::PageDown: Move(PageRows(), ui); return true;
    case Key::Home: Select(0, &ui); return true;
    case Key::End: Select(count - 1, &ui); return true;
    case Key::Enter:
        if (selected_ >= 0 && onActivate && !event.repeat) onActivate(selected_, ui);
        return selected_ >= 0;
    default:
        return false;
    }
}

auto ListView::Pad(const PadEvent& event, Ui& ui) -> bool {
    switch (event.button) {
    case pad::Button::Up:
        // Up from the first row leaves the list (the wiki's ListStep); a held Up stops there.
        if (count <= 0 || selected_ <= 0) return event.repeat && count > 0;
        Move(-1, ui);
        return true;
    case pad::Button::Down:
        if (count <= 0) return false;
        Move(1, ui);
        return true;
    case pad::Button::LB:
        if (count <= 0) return false;
        Move(-PageRows(), ui);
        return true;
    case pad::Button::RB:
        if (count <= 0) return false;
        Move(PageRows(), ui);
        return true;
    case pad::Button::A:
        if (selected_ < 0 || !onActivate) return false;
        onActivate(selected_, ui);
        return true;
    default:
        return false;
    }
}

void ListView::HelpRows(std::vector<HelpRow>& out) const {
    out.push_back({Glyph::DPadUpDown, "Up / Down", "Pick a row"});
    if (onActivate) out.push_back({Glyph::A, "Enter", "Open"});
    out.push_back({Glyph::LBRB, "PgUp / PgDn", "Page up / down"});
}

auto ListView::TooltipAt(int x, int y, Box& anchor) const -> std::string {
    const Box view = ViewBox();
    if (!rowTooltip || !view.Contains(x, y)) return {};
    const int row = RowAt(y - view.top, count, Pitch(), scroll_);
    if (row < 0 || !RowBox(row).Contains(x, y)) return {};
    anchor = RowBox(row).Intersect(view);
    return rowTooltip(row);
}

auto ListView::FocusTooltip(Box& anchor) const -> std::string {
    if (!rowTooltip || selected_ < 0) return {};
    anchor = FocusBox();
    return rowTooltip(selected_);
}

}  // namespace esrfe::overlay
