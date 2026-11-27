// The controller rules are the wiki overlay's PadTick / PadDirection / PadConfirm / PadBack (overlay.cpp) made general:
// the wiki's fixed zones (tabs, list, cards, chips ...) became "the focused widget first, then the nearest widget".
// The on-screen keyboard handling is its PadTick keyboard branch and DrawOsk; the tooltip placement is its DrawTooltip.
#include "form.hpp"

#include <algorithm>
#include <cstdlib>

#include "focus_nav.hpp"
#include "help.hpp"
#include "ui_text.hpp"
#include "utf.hpp"

namespace esrfe::overlay {
namespace {

using namespace text_flags;

auto DirectionOf(pad::Button b, int& dx, int& dy) -> bool {
    dx = dy = 0;
    switch (b) {
    case pad::Button::Left: dx = -1; return true;
    case pad::Button::Right: dx = 1; return true;
    case pad::Button::Up: dy = -1; return true;
    case pad::Button::Down: dy = 1; return true;
    default: return false;
    }
}

}  // namespace

Form::Form(Platform& platform, std::shared_ptr<View> view) : platform_(platform), view_(std::move(view)) {}

// ---- widget list ---------------------------------------------------------------------------------------------------

void Form::Refresh() {
    widgets_.clear();
    view_->FocusList(widgets_);
    if (focus_ != nullptr && !Has(focus_)) focus_ = nullptr;
    if (hover_ != nullptr && !Has(hover_)) hover_ = nullptr;
    if (capture_ != nullptr && !Has(capture_)) capture_ = nullptr;
    if (pressed_ != nullptr && !Has(pressed_)) pressed_ = nullptr;
    if (lastDownWidget_ != nullptr && !Has(lastDownWidget_)) lastDownWidget_ = nullptr;
    if (oskTarget_ != nullptr && (!Has(oskTarget_) || !oskTarget_->visible)) CloseKeyboard();
    if (choices_ != nullptr && (!Has(choices_) || !choices_->visible)) choices_ = nullptr;
    if (focus_ != nullptr && !focus_->Focusable()) focus_ = nullptr;  // hidden or disabled since
}

auto Form::Has(const Widget* widget) const -> bool {
    return std::find(widgets_.begin(), widgets_.end(), widget) != widgets_.end();
}

auto Form::WidgetAt(int x, int y) const -> Widget* {
    for (auto it = widgets_.rbegin(); it != widgets_.rend(); ++it) {  // the last drawn is on top
        Widget* w = *it;
        if (w->visible && w->HitTest(x, y)) return w;
    }
    return nullptr;
}

auto Form::FirstFocusable() const -> Widget* {
    for (Widget* w : widgets_)
        if (w->Focusable()) return w;
    return nullptr;
}

void Form::Layout(const Box& client, const Box& content) {
    client_ = client;
    content_ = content;
    LayoutContext context{client, content, platform_.GetMeasurer()};
    view_->Layout(context);
    Refresh();
    if (choices_ != nullptr) choices_->PopupPlace(client_, platform_.GetMeasurer());
}

// ---- focus ---------------------------------------------------------------------------------------------------------

void Form::Focus(Widget* widget) {
    if (widget != nullptr && (!Has(widget) || !widget->Focusable())) return;
    if (widget == focus_) return;
    Widget* old = focus_;
    focus_ = widget;
    if (old != nullptr) old->FocusChanged(false, *this);
    if (oskTarget_ != nullptr && oskTarget_ != widget) CloseKeyboard();
    if (widget != nullptr) widget->FocusChanged(true, *this);
    Repaint();
}

auto Form::MoveFocus(int dx, int dy) -> bool {
    if (focus_ == nullptr) {
        Widget* first = view_->InitialFocus();
        if (first == nullptr || !Has(first) || !first->Focusable()) first = FirstFocusable();
        if (first == nullptr) return false;
        Focus(first);
        return true;
    }
    std::vector<Box> boxes;
    std::vector<bool> enabled;
    int current = -1;
    for (std::size_t i = 0; i < widgets_.size(); ++i) {
        boxes.push_back(widgets_[i]->box);
        enabled.push_back(widgets_[i]->Focusable());
        if (widgets_[i] == focus_) current = static_cast<int>(i);
    }
    const int next = NavNeighbor(boxes, current, dx, dy, enabled);
    if (next < 0) return false;
    Focus(widgets_[static_cast<std::size_t>(next)]);
    return true;
}

auto Form::TabFocus(int direction) -> bool {
    std::vector<bool> enabled;
    int current = -1;
    for (std::size_t i = 0; i < widgets_.size(); ++i) {
        enabled.push_back(widgets_[i]->Focusable());
        if (widgets_[i] == focus_) current = static_cast<int>(i);
    }
    const int next = TabStep(enabled, current, direction);
    if (next < 0) return false;
    Focus(widgets_[static_cast<std::size_t>(next)]);
    return true;
}

void Form::FocusFromPoint(int x, int y) {
    Refresh();
    Widget* w = WidgetAt(x, y);
    if (w == nullptr || !w->Focusable()) return;
    Focus(w);
    w->PointFocus(x, y, *this);
}

// ---- keyboard, choices, dialog -------------------------------------------------------------------------------------

void Form::OpenKeyboard(TextBox& target) {
    Refresh();
    if (!Has(&target)) return;
    if (focus_ != &target) Focus(&target);
    if (!osk_.IsOpen() || oskTarget_ != &target) osk_.Open(platform_.KeyboardLocaleCode());
    oskTarget_ = &target;
    Repaint();
}

void Form::CloseKeyboard() {
    if (oskTarget_ == nullptr && !osk_.IsOpen()) return;
    osk_.Close();
    oskTarget_ = nullptr;
    oskGeometry_ = KeyboardGeometry{};
    Repaint();
}

void Form::OpenChoices(Dropdown& dropdown) {
    Refresh();
    if (!Has(&dropdown)) return;
    CloseKeyboard();
    if (focus_ != &dropdown) Focus(&dropdown);
    choices_ = &dropdown;
    dropdown.PopupOpened(client_, platform_.GetMeasurer());
    Repaint();
}

void Form::ShowDialog(DialogSpec spec, std::function<void(bool)> done) {
    CloseKeyboard();
    choices_ = nullptr;
    capture_ = nullptr;
    dialog_.emplace();
    DialogState& d = *dialog_;
    d.spec = std::move(spec);
    d.done = std::move(done);
    d.confirm.label = d.spec.confirm;
    d.confirm.style = d.spec.danger ? Button::Style::Danger : Button::Style::Primary;
    d.cancel.label = d.spec.cancel;
    d.focus = d.spec.confirmIsDefault || d.spec.cancel.empty() ? 1 : 0;
    Repaint();
}

void Form::FinishDialog(bool confirmed) {
    if (!dialog_) return;
    std::function<void(bool)> done = std::move(dialog_->done);
    dialog_.reset();
    Repaint();
    if (done) done(confirmed);  // may open another dialog
}

auto Form::DialogPad(const PadEvent& event) -> bool {
    DialogState& d = *dialog_;
    const bool two = !d.spec.cancel.empty();
    switch (event.button) {
    case pad::Button::Left:
    case pad::Button::Up:
        if (two) d.focus = 0;
        break;
    case pad::Button::Right:
    case pad::Button::Down:
        d.focus = 1;
        break;
    case pad::Button::A: FinishDialog(d.focus == 1); return true;
    case pad::Button::B: FinishDialog(false); return true;
    default: break;
    }
    Repaint();
    return true;
}

auto Form::DialogKey(const KeyEvent& event) -> bool {
    DialogState& d = *dialog_;
    const bool two = !d.spec.cancel.empty();
    switch (event.key) {
    case Key::Tab:
    case Key::Left:
    case Key::Right:
        if (two) d.focus = 1 - d.focus;
        keyboardNav_ = true;
        break;
    case Key::Enter:
    case Key::Space:
        if (!event.repeat) FinishDialog(d.focus == 1);
        return true;
    case Key::Escape: FinishDialog(false); return true;
    default: break;
    }
    Repaint();
    return true;
}

auto Form::DialogMouse(const MouseEvent& event) -> bool {
    DialogState& d = *dialog_;
    if (event.kind == MouseKind::Down) {
        if (d.confirm.box.Contains(event.x, event.y)) FinishDialog(true);
        else if (!d.spec.cancel.empty() && d.cancel.box.Contains(event.x, event.y)) FinishDialog(false);
    } else if (event.kind == MouseKind::Move) {
        const int over = d.confirm.box.Contains(event.x, event.y) ? 1 : (d.cancel.box.Contains(event.x, event.y) ? 0 : -1);
        if (over >= 0 && over != d.focus) {
            d.focus = over;
            Repaint();
        }
    }
    return true;  // modal
}

void Form::ApplyKey(const OnScreenKeyboard::Result& result) {
    TextBox* target = oskTarget_;
    if (target == nullptr) return;
    bool changed = false;
    switch (result.action) {
    case OnScreenKeyboard::Action::Type: {
        const char16_t one[1] = {result.ch};
        changed = target->edit.Insert(std::u16string_view(one, 1));
        break;
    }
    case OnScreenKeyboard::Action::Backspace: changed = target->edit.Backspace(); break;
    case OnScreenKeyboard::Action::Clear: changed = target->edit.Clear(); break;
    case OnScreenKeyboard::Action::Done: CloseKeyboard(); break;
    case OnScreenKeyboard::Action::Toggled:
    case OnScreenKeyboard::Action::None: break;
    }
    if (changed) target->Changed(*this);
    Repaint();
}

// The wiki's keyboard branch of PadTick.
auto Form::KeyboardPad(const PadEvent& event) -> bool {
    TextBox* target = oskTarget_;
    int dx = 0, dy = 0;
    if (DirectionOf(event.button, dx, dy)) {
        if (dy > 0 && osk_.AtBottomRow() && target->keyboardExitDown) {
            // Down from the bottom row leaves the keyboard toward what is under the text box; a held Down stops there.
            if (event.repeat) return true;
            CloseKeyboard();
            MoveFocus(0, 1);
            return true;
        }
        osk_.Move(dx, dy);
        Repaint();
        return true;
    }
    switch (event.button) {
    case pad::Button::A: ApplyKey(osk_.PressHighlighted()); return true;
    case pad::Button::X: ApplyKey(OnScreenKeyboard::Result{OnScreenKeyboard::Action::Backspace, 0}); return true;  // Square / X deletes one
    case pad::Button::Y: CloseKeyboard(); return true;
    case pad::Button::B:
        // The wiki's search box: B clears the text, and with nothing typed puts the keyboard away. Other boxes: B puts
        // the keyboard away and keeps the text.
        if (target->search && !target->edit.Text().empty()) {
            if (target->edit.Clear()) target->Changed(*this);
        } else {
            CloseKeyboard();
        }
        return true;
    default:
        return false;  // LB / RB and the rest go on to the view (the wiki changes tabs with the keyboard open)
    }
}

// ---- input ---------------------------------------------------------------------------------------------------------

void Form::UpdateHover(int x, int y) {
    Widget* now = WidgetAt(x, y);
    if (now != hover_) {
        if (hover_ != nullptr) hover_->Leave(*this);
        hover_ = now;
        Repaint();
    }
}

auto Form::Mouse(const MouseEvent& in) -> bool {
    Refresh();
    MouseEvent event = in;
    if (event.kind != MouseKind::Leave) {
        mouseInside_ = true;
        // No repaint for a move as such: tooltips sit beside their target, not at the pointer, so only a change of
        // target repaints (UpdateHover, or a list's hovered row), as the wiki does.
        mouseX_ = event.x;
        mouseY_ = event.y;
    }
    if (event.kind == MouseKind::Down || event.kind == MouseKind::Wheel) keyboardNav_ = false;
    if (dialog_) return DialogMouse(event);
    if (choices_ != nullptr) {
        if (event.kind == MouseKind::Wheel) return choices_->PopupWheel(event.wheelSteps, *this);
        if (!choices_->PopupMouse(event, *this) && event.kind == MouseKind::Down) {
            CloseChoices();  // a click outside closes the list and does nothing else
            Repaint();
        }
        return true;
    }
    if (event.kind == MouseKind::Leave) {
        mouseInside_ = false;
        if (hover_ != nullptr) hover_->Leave(*this);
        hover_ = nullptr;
        Repaint();
        return true;
    }
    if (oskTarget_ != nullptr && event.kind == MouseKind::Down) {
        if (oskGeometry_.box.Contains(event.x, event.y)) {
            for (const KeyboardGeometry::Key& key : oskGeometry_.keys) {
                if (key.box.Contains(event.x, event.y)) ApplyKey(osk_.Press(key.row, key.col));
            }
            return true;
        }
        if (!oskTarget_->box.Contains(event.x, event.y)) CloseKeyboard();  // a click anywhere else puts it away
    }
    if (capture_ != nullptr && (event.kind == MouseKind::Move || event.kind == MouseKind::Up)) {
        Widget* captured = capture_;
        if (event.kind == MouseKind::Up) pressed_ = nullptr;
        captured->Mouse(event, *this);
        return true;
    }
    switch (event.kind) {
    case MouseKind::Move: {
        UpdateHover(event.x, event.y);
        if (hover_ != nullptr) hover_->Mouse(event, *this);
        return hover_ != nullptr;
    }
    case MouseKind::Down: {
        Widget* w = WidgetAt(event.x, event.y);
        // A second press soon after on the same widget, near the first, is a double click.
        const std::uint64_t now = Now();
        const bool isDouble = !lastWasDouble_ && w != nullptr && w == lastDownWidget_ && now - lastDownAt_ <= platform_.DoubleClickMs() &&
                              std::abs(event.x - lastDownX_) <= 4 && std::abs(event.y - lastDownY_) <= 4;
        event.clicks = isDouble ? 2 : 1;
        lastWasDouble_ = isDouble;
        lastDownAt_ = now;
        lastDownX_ = event.x;
        lastDownY_ = event.y;
        lastDownWidget_ = w;
        if (w == nullptr || !w->enabled) return w != nullptr;
        if (w->Focusable()) Focus(w);
        pressed_ = w;
        const bool used = w->Mouse(event, *this);
        Repaint();
        return used;
    }
    case MouseKind::Up: {
        pressed_ = nullptr;
        Widget* w = WidgetAt(event.x, event.y);
        if (w != nullptr) w->Mouse(event, *this);
        Repaint();
        return w != nullptr;
    }
    case MouseKind::Wheel: {
        Widget* w = WidgetAt(event.x, event.y);
        return w != nullptr && w->enabled && w->Wheel(event.wheelSteps, event.x, event.y, *this);
    }
    case MouseKind::Leave: break;
    }
    return false;
}

auto Form::KeyDown(const KeyEvent& event) -> bool {
    Refresh();
    if (dialog_) return DialogKey(event);
    if (choices_ != nullptr) return choices_->PopupKey(event, *this);
    if (focus_ != nullptr && focus_->enabled && focus_->Key(event, *this)) return true;
    if (view_->Key(event, *this)) return true;
    switch (event.key) {
    case Key::Tab:
        keyboardNav_ = true;
        TabFocus(event.mods.shift ? -1 : 1);
        return true;
    case Key::Left:
    case Key::Right:
    case Key::Up:
    case Key::Down: {
        keyboardNav_ = true;
        const int dx = event.key == Key::Left ? -1 : (event.key == Key::Right ? 1 : 0);
        const int dy = event.key == Key::Up ? -1 : (event.key == Key::Down ? 1 : 0);
        MoveFocus(dx, dy);
        Repaint();
        return true;
    }
    case Key::Escape:
        return Back();
    default:
        return false;
    }
}

auto Form::Char(char16_t c) -> bool {
    Refresh();
    if (dialog_ || choices_ != nullptr) return true;
    if (focus_ != nullptr && focus_->enabled) return focus_->Char(c, *this);
    return false;
}

auto Form::PadPress(const PadEvent& event) -> bool {
    Refresh();
    if (dialog_) return DialogPad(event);
    if (choices_ != nullptr) return choices_->PopupPad(event, *this);
    if (oskTarget_ != nullptr && osk_.IsOpen()) {
        if (KeyboardPad(event)) return true;
        return view_->Pad(event, *this);
    }
    int dx = 0, dy = 0;
    const bool direction = DirectionOf(event.button, dx, dy);
    if (direction && focus_ == nullptr) return MoveFocus(dx, dy);  // the first press highlights something
    if (focus_ != nullptr && focus_->enabled && focus_->Pad(event, *this)) return true;
    if (view_->Pad(event, *this)) return true;
    if (direction) {
        MoveFocus(dx, dy);
        Repaint();
        return true;
    }
    switch (event.button) {
    case pad::Button::Y: {
        TextBox* search = view_->SearchBox();
        if (search == nullptr) return false;
        Refresh();
        if (!Has(search)) return false;
        OpenKeyboard(*search);
        return true;
    }
    case pad::Button::B:
        return Back();
    default:
        return false;
    }
}

auto Form::Back() -> bool {
    if (dialog_) {
        FinishDialog(false);
        return true;
    }
    if (choices_ != nullptr) {
        CloseChoices();
        Repaint();
        return true;
    }
    if (oskTarget_ != nullptr) {
        CloseKeyboard();
        return true;
    }
    return view_->Back(*this);
}

auto Form::ScrollBy(int pixels) -> bool {
    Refresh();
    if (dialog_) return false;
    if (choices_ != nullptr) return false;
    // The focused widget, else the one under the pointer, else the first one that scrolls.
    if (focus_ != nullptr && focus_->ScrollBy(pixels, *this)) return true;
    if (mouseInside_) {
        Widget* under = WidgetAt(mouseX_, mouseY_);
        if (under != nullptr && under != focus_ && under->ScrollBy(pixels, *this)) return true;
    }
    for (Widget* w : widgets_)
        if (w != focus_ && w->visible && w->ScrollBy(pixels, *this)) return true;
    return false;
}

auto Form::TextInputCaret(Box& caret) -> bool {
    if (dialog_ || choices_ != nullptr || focus_ == nullptr) return false;
    TextBox* box = focus_->TextInput();
    if (box == nullptr) return false;
    caret = box->CaretBox(platform_.GetMeasurer());
    return true;
}

void Form::CollectHelp(std::vector<HelpRow>& out) {
    Refresh();
    if (dialog_) {
        const auto rows = DialogHelpRows();
        out.insert(out.end(), rows.begin(), rows.end());
        return;
    }
    if (choices_ != nullptr) {
        const auto rows = PopupHelpRows();
        out.insert(out.end(), rows.begin(), rows.end());
        return;
    }
    if (oskTarget_ != nullptr) {
        const auto rows = KeyboardPadRows(oskTarget_->keyboardExitDown);
        out.insert(out.end(), rows.begin(), rows.end());
        return;
    }
    if (focus_ != nullptr) focus_->HelpRows(out);
    view_->HelpRows(focus_, out);
    out.push_back({Glyph::DPad, "Tab / Arrows", "Highlight"});
    if (view_->SearchBox() != nullptr) out.push_back({Glyph::Y, "", "Search"});
    out.push_back({Glyph::B, "", "Back"});
    out.push_back({Glyph::None, "Esc", "Close"});
}

void Form::Shown() {
    Refresh();
    view_->Shown(*this);
    Refresh();
    if (focus_ == nullptr) {
        Widget* first = view_->InitialFocus();
        if (first != nullptr) Focus(first);
    }
}

void Form::Hidden() {
    CloseKeyboard();
    choices_ = nullptr;
    capture_ = nullptr;
    pressed_ = nullptr;
    if (hover_ != nullptr) hover_->Leave(*this);
    hover_ = nullptr;
    mouseInside_ = false;
    view_->Hidden(*this);
}

void Form::PadModeChanged(bool active) {
    if (!active) CloseKeyboard();  // the mouse or the keyboard was used: the on-screen keyboard goes (the wiki's PadMouseUsed)
    Repaint();
}

// ---- painting ------------------------------------------------------------------------------------------------------

void Form::Paint(Canvas& canvas) {
    Refresh();
    canvas.SetHighlight(nullptr);
    view_->Paint(canvas);
    const bool padActive = PadActive();
    for (Widget* w : widgets_) {
        if (!w->visible) continue;
        PaintState state;
        state.focused = w == focus_;
        state.hovered = w == hover_ && mouseInside_ && !padActive;
        state.pressed = w == pressed_;
        state.padActive = padActive;
        w->Paint(canvas, state);
    }
    view_->PaintOver(canvas);
    canvas.SetHighlight(nullptr);  // a view that marks search matches sets it for its own text only
    // The bright frame round what is highlighted, while the controller (or the keyboard) moves the highlight.
    if (focus_ != nullptr && focus_->visible && (padActive || keyboardNav_) && oskTarget_ == nullptr && !dialog_ && choices_ == nullptr)
        PaintFocusFrame(canvas, focus_->FocusBox());
    if (oskTarget_ != nullptr && osk_.IsOpen()) PaintKeyboard(canvas);
    if (choices_ != nullptr) choices_->PaintPopup(canvas);
    if (dialog_) PaintDialog(canvas);
    PaintTooltip(canvas);
}

// The wiki's DrawOsk.
void Form::PaintKeyboard(Canvas& canvas) {
    const Theme& t = canvas.GetTheme();
    // The keyboard is not page text: the search highlight must not mark its keys (typing "a" would light up "a").
    canvas.SetHighlight(nullptr);
    KeyboardMetrics m;
    m.unit = canvas.S(22);
    m.keyHeight = std::max(canvas.S(30), canvas.LineHeight(Font::Body) + canvas.S(12));
    m.gap = canvas.S(4);
    m.pad = canvas.S(8);
    m.edge = canvas.S(8);
    m.offset = canvas.S(4);
    oskGeometry_ = LayoutKeyboard(osk_.Rows(), oskTarget_->box, client_, m);
    canvas.DropShadow(oskGeometry_.box, canvas.S(3));
    canvas.FillAlpha(oskGeometry_.box, t.tooltipFill, t.popupAlpha);
    canvas.Frame(oskGeometry_.box, t.gold);
    const bool padActive = PadActive();
    for (const KeyboardGeometry::Key& key : oskGeometry_.keys) {
        const pad::OskKey& k = osk_.Rows()[static_cast<std::size_t>(key.row)][static_cast<std::size_t>(key.col)];
        const bool focus = padActive && key.row == osk_.Row() && key.col == osk_.Col();
        canvas.FillAlpha(key.box, focus ? t.selected : t.panel, t.keyAlpha);
        canvas.Frame(key.box, focus ? t.gold : t.border);
        if (focus) canvas.Frame(key.box.Inflated(1), t.focus);
        std::string label = WideToUtf8(k.label);
        if (k.kind == pad::KeyKind::Space) label = UiText("Space");
        else if (k.kind == pad::KeyKind::Clear) label = UiText("Clear");
        else if (k.kind == pad::KeyKind::Done) label = UiText("Done");
        canvas.Text(label, key.box, focus ? t.headerGold : t.text, Font::Body, kCenter | kVCenter | kSingleLine | kRaw | kNoHighlight);
    }
}

void Form::PaintDialog(Canvas& canvas) {
    const Theme& t = canvas.GetTheme();
    DialogState& d = *dialog_;
    canvas.SetHighlight(nullptr);
    canvas.FillAlpha(client_, Rgb{0, 0, 0}, t.dimAlpha);
    const int width = std::min(canvas.S(520), client_.Width() - canvas.S(40));
    const int pad = canvas.S(16);
    const int inner = width - 2 * pad;
    const std::string title = UiText(d.spec.title);
    const int titleHeight = title.empty() ? 0 : canvas.LineHeight(Font::Title) + canvas.S(8);
    const int messageHeight = d.spec.message.empty() ? 0 : canvas.TextHeight(d.spec.message, inner, Font::Body);
    const int buttonHeight = std::max(canvas.S(30), canvas.LineHeight(Font::Body) + canvas.S(10));
    const int height = pad + titleHeight + messageHeight + canvas.S(16) + buttonHeight + pad;
    const int left = client_.left + (client_.Width() - width) / 2;
    const int top = client_.top + std::max(canvas.S(8), (client_.Height() - height) / 2);
    d.box = Box{left, top, left + width, top + height};
    canvas.DropShadow(d.box, canvas.S(3));
    canvas.FillAlpha(d.box, t.tooltipFill, t.popupAlpha);
    canvas.Frame(d.box, t.gold);
    int y = top + pad;
    if (!title.empty()) {
        canvas.Text(title, Box{left + pad, y, left + pad + inner, y + titleHeight}, t.headerGold, Font::Title, kSingleLine | kEllipsis);
        y += titleHeight;
    }
    if (messageHeight > 0) canvas.Text(d.spec.message, Box{left + pad, y, left + pad + inner, y + messageHeight}, t.body, Font::Body, kLeft);
    const int buttonTop = d.box.bottom - pad - buttonHeight;
    const auto buttonWidth = [&](const std::string& label) { return std::max(canvas.S(110), canvas.TextWidth(UiText(label), Font::Body) + canvas.S(32)); };
    const int confirmWidth = buttonWidth(d.spec.confirm);
    d.confirm.box = Box{d.box.right - pad - confirmWidth, buttonTop, d.box.right - pad, buttonTop + buttonHeight};
    if (!d.spec.cancel.empty()) {
        const int cancelWidth = buttonWidth(d.spec.cancel);
        d.cancel.box = Box{d.confirm.box.left - canvas.S(8) - cancelWidth, buttonTop, d.confirm.box.left - canvas.S(8), buttonTop + buttonHeight};
        PaintState cancelState;
        cancelState.focused = d.focus == 0;
        d.cancel.Paint(canvas, cancelState);
    } else {
        d.cancel.box = Box{};
    }
    PaintState confirmState;
    confirmState.focused = d.focus == 1;
    d.confirm.Paint(canvas, confirmState);
    PaintFocusFrame(canvas, d.focus == 1 ? d.confirm.box : d.cancel.box);
}

// The wiki's DrawTooltip, for one text: beside the thing hovered, not at the pointer, and clear of the drawn cursor;
// as wide as its longest line needs (within limits), wrapped beyond that, kept inside the window.
void Form::PaintTooltip(Canvas& canvas) {
    if (dialog_ || choices_ != nullptr || oskTarget_ != nullptr) return;
    Box anchor{};
    std::string text;
    if (PadActive()) {
        if (focus_ != nullptr && focus_->visible) text = focus_->FocusTooltip(anchor);
    } else if (mouseInside_ && capture_ == nullptr) {
        Widget* under = WidgetAt(mouseX_, mouseY_);
        if (under != nullptr) text = under->TooltipAt(mouseX_, mouseY_, anchor);
    }
    if (text.empty()) return;
    const Theme& t = canvas.GetTheme();
    canvas.SetHighlight(nullptr);
    std::vector<std::string> lines;
    for (std::size_t start = 0;;) {
        const std::size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    const int gap = canvas.S(2);
    int widest = 0;
    for (const std::string& line : lines) widest = std::max(widest, canvas.TextWidth(line, Font::Body));
    const int maxTipWidth = std::max(canvas.S(160), client_.Width() - canvas.S(16));
    const int tipWidth = std::min(std::max(widest + canvas.S(24) + canvas.S(8), canvas.S(160)), std::min(maxTipWidth, canvas.S(640)));
    const int inner = tipWidth - canvas.S(24);
    std::vector<int> heights;
    int total = canvas.S(16);
    for (const std::string& line : lines) {
        const int h = line.empty() ? canvas.LineHeight(Font::Body) : canvas.TextHeight(line, inner, Font::Body);
        heights.push_back(h);
        total += h + gap;
    }
    const Box reach = platform_.CursorReach();
    const int cursorRight = std::max(reach.right, 0) + canvas.S(6);
    const int cursorLeft = std::max(reach.left, 0) + canvas.S(6);
    int left = anchor.right + cursorRight;
    if (left + tipWidth > client_.right - canvas.S(8)) {
        const int leftSide = anchor.left - cursorLeft - tipWidth;
        left = leftSide >= client_.left + canvas.S(8) ? leftSide : std::max(client_.left + canvas.S(8), client_.right - canvas.S(8) - tipWidth);
    }
    int top = anchor.top;
    if (top + total > client_.bottom - canvas.S(8)) top = client_.bottom - total - canvas.S(8);
    top = std::max(client_.top + canvas.S(8), top);
    const int visibleHeight = std::min(total, client_.bottom - top - canvas.S(8));
    const Box box{left, top, left + tipWidth, top + visibleHeight};
    // Tooltips sit on a see-through black so the window behind them still shows.
    canvas.CornerShadow(box, canvas.S(2));
    canvas.FillAlpha(box, t.tooltipFill, t.tooltipAlpha);
    canvas.Frame(box, t.gold);
    canvas.PushClip(box);
    int y = top + canvas.S(8);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        canvas.Text(lines[i], Box{left + canvas.S(12), y, left + canvas.S(12) + inner, y + heights[i]}, i == 0 ? t.gold : t.body, Font::Body, kLeft);
        y += heights[i] + gap;
    }
    canvas.PopClip();
}

}  // namespace esrfe::overlay
